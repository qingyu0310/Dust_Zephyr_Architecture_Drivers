/**
 * @file usb_device.cpp
 * @author qingyu
 * @brief USB 设备核心实现 — EP0 控制传输 + 标准请求处理
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#include "usb_device.hpp"

#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_device, LOG_LEVEL_INF);

/**
 * @brief 初始化
 * @param hal      HAL 实例
 * @param function 功能类
 * @param cfg      HAL 配置
 * @return true=成功
 */
bool UsbDevice::Init(UsbHal& hal, UsbFunction& function, const UsbHal::Config& cfg)
{
    if (ready_) {
        return true;
    }

    hal_      = &hal;
    function_ = &function;

    if (!hal_->Init(cfg, HalEvent, this)) {
        return false;
    }

    ready_ = true;
    return true;
}

/**
 * @brief 启动 USB（连接主机）
 */
bool UsbDevice::Start()
{
    if (!ready_) {
        return false;
    }
    return hal_->Connect();
}

/**
 * @brief 停止 USB
 */
void UsbDevice::Stop()
{
    ready_ = false;
    hal_->Disconnect();
}

// ============================================================================
//  事件分发
// ============================================================================

/**
 * @brief HAL 事件回调（静态，转进对象方法）
 */
void UsbDevice::HalEvent(void* context, const UsbHal::Event& event)
{
    if (context != nullptr) {
        static_cast<UsbDevice*>(context)->OnEvent(event);
    }
}

/**
 * @brief 事件路由
 */
void UsbDevice::OnEvent(const UsbHal::Event& event)
{
    switch (event.type)
    {
    case UsbHal::EventType::Reset:
    case UsbHal::EventType::Disconnected:
        ResetState();
        function_->OnConfigured(false);
        break;

    case UsbHal::EventType::SetupReceived:
        HandleSetup(event.setup);
        break;

    case UsbHal::EventType::TransferComplete:
        HandleTransferComplete(event);
        break;

    case UsbHal::EventType::Connected:
        speed_ = event.speed;
        break;

    case UsbHal::EventType::Suspend:
    case UsbHal::EventType::Resume:
        break;
    }
}

// ============================================================================
//  状态复位
// ============================================================================

/**
 * @brief 复位所有设备状态到 Default
 */
void UsbDevice::ResetState()
{
    state_          = usb::DeviceState::Default;
    address_        = 0;
    configuration_  = 0;
    ep0_stage_      = Ep0Stage::Idle;
    control_len_    = 0;
}

// ============================================================================
//  SETUP 处理
// ============================================================================

/**
 * @brief 处理收到的 SETUP 包
 * @param setup_data  8 字节 SETUP 数据
 */
void UsbDevice::HandleSetup(const uint8_t setup_data[8])
{
    // 反序列化 SETUP 包
    usb::SetupPacket setup {};
    setup.bm_request_type = setup_data[0];
    setup.b_request       = setup_data[1];
    setup.w_value         = static_cast<uint16_t>(setup_data[2])
                          | (static_cast<uint16_t>(setup_data[3]) << 8);
    setup.w_index         = static_cast<uint16_t>(setup_data[4])
                          | (static_cast<uint16_t>(setup_data[5]) << 8);
    setup.w_length        = static_cast<uint16_t>(setup_data[6])
                          | (static_cast<uint16_t>(setup_data[7]) << 8);

    // EP0 重新 idle
    ep0_stage_ = Ep0Stage::Idle;

    if (setup.IsStandard()) {
        HandleStandardRequest(setup);
    } else if (setup.IsClass()) {
        HandleClassRequest(setup);
    } else {
        // 不支持的请求 → STALL
        hal_->EpStall(0x00, true);
    }
}

// ============================================================================
//  标准请求处理
// ============================================================================

/**
 * @brief 处理标准请求
 */
void UsbDevice::HandleStandardRequest(const usb::SetupPacket& setup)
{
    switch (static_cast<usb::StandardRequest>(setup.b_request))
    {
    case usb::StandardRequest::GetStatus:
    {
        // 返回 2 字节状态
        uint16_t status = 0;
        if (setup.Recipient() == usb::kRecipientEndpoint) {
            // bit0 = halt（当前不支持端点 halt 查询，始终 0）
        }
        control_buffer_[0] = static_cast<uint8_t>(status & 0xFF);
        control_buffer_[1] = static_cast<uint8_t>((status >> 8) & 0xFF);
        ep0_stage_ = Ep0Stage::DataIn;
        if (!hal_->Ep0StartIn(control_buffer_, 2)) {
            ep0_stage_ = Ep0Stage::Idle;
            hal_->EpStall(0x00, true);
        }
        break;
    }

    case usb::StandardRequest::ClearFeature:
    {
        if (setup.Recipient() == usb::kRecipientEndpoint &&
            setup.w_value == static_cast<uint16_t>(usb::FeatureSelector::EndpointHalt)) {
            uint8_t ep = static_cast<uint8_t>(setup.w_index & 0xFF);
            hal_->EpStall(ep, false);
        }
        // 其他特征选择子忽略，直接 STATUS
        SendStatusIn();
        break;
    }

    case usb::StandardRequest::SetFeature:
    {
        if (setup.Recipient() == usb::kRecipientEndpoint &&
            setup.w_value == static_cast<uint16_t>(usb::FeatureSelector::EndpointHalt)) {
            uint8_t ep = static_cast<uint8_t>(setup.w_index & 0xFF);
            hal_->EpStall(ep, true);
            SendStatusIn();
        } else {
            // 不支持 → STALL
            hal_->EpStall(0x00, true);
        }
        break;
    }

    case usb::StandardRequest::SetAddress:
    {
        // 立即写 DEVICEADDR（USBADRA=1 使地址在 STATUS IN 完成后自动生效）
        // 这与 CherryUSB 做法一致，延迟设置会导致 USBADRA 无法触发地址切换。
        uint8_t addr = static_cast<uint8_t>(setup.w_value & 0xFF);
        hal_->SetAddress(addr);
        address_ = addr;
        state_  = usb::DeviceState::Addressed;
        SendStatusIn();
        break;
    }

    case usb::StandardRequest::GetDescriptor:
    {
        if (!SendDescriptor(setup)) {
            hal_->EpStall(0x00, true);
        }
        break;
    }

    case usb::StandardRequest::SetConfiguration:
    {
        uint8_t cfg_val = static_cast<uint8_t>(setup.w_value & 0xFF);
        // 只接受配置 0（取消配置）或 1（唯一配置）
        if (cfg_val > 1) {
            hal_->EpStall(0x00, true);
            break;
        }
        configuration_ = cfg_val;
        if (configuration_ != 0) {
            if (!function_->OnConfigured(true)) {
                // 端点打开失败，拒绝配置
                configuration_ = 0;
                state_ = usb::DeviceState::Addressed;
                hal_->EpStall(0x00, true);
                break;
            }
            state_ = usb::DeviceState::Configured;
        } else {
            // configuration = 0 表示取消配置
            state_ = usb::DeviceState::Addressed;
            function_->OnConfigured(false);
        }
        SendStatusIn();
        break;
    }

    case usb::StandardRequest::GetConfiguration:
    {
        control_buffer_[0] = configuration_;
        ep0_stage_ = Ep0Stage::DataIn;
        if (!hal_->Ep0StartIn(control_buffer_, 1)) {
            ep0_stage_ = Ep0Stage::Idle;
            hal_->EpStall(0x00, true);
        }
        break;
    }

    case usb::StandardRequest::GetInterface:
    {
        // 始终返回 alternate setting 0
        control_buffer_[0] = 0;
        ep0_stage_ = Ep0Stage::DataIn;
        if (!hal_->Ep0StartIn(control_buffer_, 1)) {
            ep0_stage_ = Ep0Stage::Idle;
            hal_->EpStall(0x00, true);
        }
        break;
    }

    case usb::StandardRequest::SetInterface:
    {
        if (setup.w_value == 0) {
            SendStatusIn();
        } else {
            hal_->EpStall(0x00, true);
        }
        break;
    }

    case usb::StandardRequest::SetDescriptor:
    case usb::StandardRequest::SynchFrame:
    default:
        // 不支持 → STALL
        hal_->EpStall(0x00, true);
        break;
    }
}

// ============================================================================
//  描述符查询
// ============================================================================

/**
 * @brief 发送描述符
 * @param setup  SETUP 包（含 wValue, wLength）
 * @return true=找到描述符并已发送
 */
bool UsbDevice::SendDescriptor(const usb::SetupPacket& setup)
{
    // wValue 高字节 = 描述符类型，低字节 = 索引/语言 ID
    uint8_t desc_type  = static_cast<uint8_t>((setup.w_value >> 8) & 0xFF);
    uint8_t desc_index = static_cast<uint8_t>(setup.w_value & 0xFF);

    usb::DescriptorType type = static_cast<usb::DescriptorType>(desc_type);

    uint16_t length = 0;
    const uint8_t* data = function_->GetDescriptor(type, speed_, desc_index, length);

    if (data == nullptr || length == 0) {
        return false;
    }

    // 按 wLength 截断
    if (length > setup.w_length) {
        length = setup.w_length;
    }

    if (type == usb::DescriptorType::Device && length >= 8) {
        if (length >= 18) {
            LOG_INF("GET_DEVICE_DESC wLength=%u response=%u "
                    "data=%02x %02x %02x %02x %02x %02x %02x %02x "
                    "%02x %02x %02x %02x %02x %02x %02x %02x "
                    "%02x %02x",
                    setup.w_length,
                    length,
                    data[0], data[1], data[2], data[3],
                    data[4], data[5], data[6], data[7],
                    data[8], data[9], data[10], data[11],
                    data[12], data[13], data[14], data[15],
                    data[16], data[17]);
        } else {
            LOG_INF("GET_DEVICE_DESC wLength=%u response=%u "
                    "data=%02x %02x %02x %02x %02x %02x %02x %02x",
                    setup.w_length,
                    length,
                    data[0], data[1], data[2], data[3],
                    data[4], data[5], data[6], data[7]);
        }
    } else {
        LOG_INF("GET_DESCRIPTOR type=%u index=%u wLength=%u response=%u",
                desc_type, desc_index, setup.w_length, length);
    }

    // Ep0StartIn 内部已使用 nocache 缓冲发送，不需先拷贝到 control_buffer_
    ep0_stage_ = Ep0Stage::DataIn;
    return hal_->Ep0StartIn(data, length);
}

// ============================================================================
//  Class 请求分发
// ============================================================================

/**
 * @brief 将 Class 请求分发给功能类
 */
void UsbDevice::HandleClassRequest(const usb::SetupPacket& setup)
{
    if (function_ == nullptr) {
        hal_->EpStall(0x00, true);
        return;
    }

    // 保存当前 setup 包，DataOut 完成时用于回调
    class_setup_ = setup;

    // 准备 control_buffer 用于 DATA OUT
    uint16_t len = setup.w_length;
    if (len > sizeof(control_buffer_)) {
        len = sizeof(control_buffer_);
    }

    if (function_->HandleClassRequest(setup, control_buffer_, len)) {
        // Class 请求已处理
        if (setup.IsDeviceToHost()) {
            ep0_stage_ = Ep0Stage::DataIn;
            if (!hal_->Ep0StartIn(control_buffer_, len)) {
                ep0_stage_ = Ep0Stage::Idle;
                hal_->EpStall(0x00, true);
            }
        } else {
            if (len > 0) {
                ep0_stage_ = Ep0Stage::DataOut;
                if (!hal_->Ep0StartOut(control_buffer_, len)) {
                    ep0_stage_ = Ep0Stage::Idle;
                    hal_->EpStall(0x00, true);
                }
            } else {
                SendStatusIn();
            }
        }
    } else {
        // 不支持 → STALL
        hal_->EpStall(0x00, true);
    }
}

// ============================================================================
//  传输完成处理
// ============================================================================

/**
 * @brief 处理传输完成事件
 *
 * 区分 EP0 各阶段和功能类端点。
 */
void UsbDevice::HandleTransferComplete(const UsbHal::Event& event)
{
    // EP0 控制传输阶段推进
    if (event.endpoint == 0x00 || event.endpoint == 0x80) {
        switch (ep0_stage_)
        {
        case Ep0Stage::DataIn:
            // DATA IN 完成 → 等待 STATUS OUT
            ep0_stage_ = Ep0Stage::StatusOut;
            if (!hal_->Ep0StatusOut()) {
                LOG_ERR("EP0 StatusOut failed after DataIn");
                ep0_stage_ = Ep0Stage::Idle;
                hal_->EpStall(0x00, true);
            }
            break;

        case Ep0Stage::DataOut:
            // DATA OUT 完成 → 通知功能类处理收到的数据
            // 错误事件或无效数据（nullptr/长度不匹配）→ STALL
            if (event.error || event.data == nullptr || event.length == 0) {
                ep0_stage_ = Ep0Stage::Idle;
                hal_->EpStall(0x00, true);
                break;
            }
            if (function_ != nullptr) {
                if (!function_->CompleteControlOut(class_setup_, event.data, event.length)) {
                    // 类驱动拒绝数据 → STALL，不进入 STATUS
                    ep0_stage_ = Ep0Stage::Idle;
                    hal_->EpStall(0x00, true);
                    break;
                }
            }
            // 发送 STATUS IN
            ep0_stage_ = Ep0Stage::StatusIn;
            if (!hal_->Ep0StatusIn()) {
                LOG_ERR("EP0 StatusIn failed after DataOut");
                ep0_stage_ = Ep0Stage::Idle;
                hal_->EpStall(0x00, true);
            }
            break;

        case Ep0Stage::StatusIn:
            // STATUS IN 完成
            ep0_stage_ = Ep0Stage::Idle;
            break;

        case Ep0Stage::StatusOut:
            // STATUS OUT 完成
            ep0_stage_ = Ep0Stage::Idle;
            break;

        case Ep0Stage::Idle:
        default:
            break;
        }
    } else {
        // 非 EP0 端点 → 分发给功能类
        if (function_ != nullptr) {
            function_->OnEndpointComplete(event.endpoint, event.data, event.length, event.error);
        }
    }
}

// ============================================================================
//  辅助
// ============================================================================

/**
 * @brief 发送 STATUS IN（无数据，空应答）
 */
bool UsbDevice::SendStatusIn()
{
    ep0_stage_ = Ep0Stage::StatusIn;
    return hal_->Ep0StatusIn();
}

/**
 * @brief 发送 STATUS OUT（无数据，空应答）
 */
bool UsbDevice::SendStatusOut()
{
    ep0_stage_ = Ep0Stage::StatusOut;
    return hal_->Ep0StatusOut();
}
