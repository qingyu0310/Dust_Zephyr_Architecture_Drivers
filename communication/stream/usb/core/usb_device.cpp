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
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_device, LOG_LEVEL_INF);

//  —————————————————————————— 初始化 ——————————————————————————

/**
 * @brief 初始化 USB 设备核心
 * @param hal      HAL 实例
 * @param function 功能类（CDC ACM 等）
 * @param cfg      HAL 配置
 */
bool UsbDevice::Init(UsbHal& hal, UsbFunction& function, const UsbHal::Config& cfg)
{
    if (ready_) return true;
    hal_      = &hal;
    function_ = &function;
    if (!hal_->Init(cfg, HalEvent, this)) return false;
    ready_ = true;
    return true;
}

//  —————————————————————————— 事件分发 ——————————————————————————

/**
 * @brief HAL 事件静态转发
 */
void UsbDevice::HalEvent(void* context, const UsbHal::Event& event)
{
    if (context) static_cast<UsbDevice*>(context)->OnEvent(event);
}

/**
 * @brief 事件路由 — Reset/Setup/TransferComplete/Connected
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
        default:
            break;
    }
}

//  —————————————————————————— SETUP 处理 ——————————————————————————

/**
 * @brief 处理收到的 SETUP 包
 * @param setup_data  8 字节 SETUP
 */
void UsbDevice::HandleSetup(const uint8_t setup_data[8])
{
    usb::SetupPacket setup {};
    setup.bm_request_type = setup_data[0];
    setup.b_request       = setup_data[1];
    setup.w_value         = static_cast<uint16_t>(setup_data[2]) | (static_cast<uint16_t>(setup_data[3]) << 8);
    setup.w_index         = static_cast<uint16_t>(setup_data[4]) | (static_cast<uint16_t>(setup_data[5]) << 8);
    setup.w_length        = static_cast<uint16_t>(setup_data[6]) | (static_cast<uint16_t>(setup_data[7]) << 8);

    ctrl_.stage = Ep0Stage::Idle;

    if (setup.IsStandard()) {
        HandleStandardRequest(setup);
    } 
    else if (setup.IsClass()) {
        HandleClassRequest(setup);
    } 
    else {
        hal_->EpStall(0x00, true);
    }
}

//  —————————————————————————— 标准请求 ——————————————————————————

/**
 * @brief 处理 USB 标准请求（GET_DESCRIPTOR/SET_ADDRESS/SET_CONFIG 等）
 */
void UsbDevice::HandleStandardRequest(const usb::SetupPacket& setup)
{
    switch (static_cast<usb::StandardRequest>(setup.b_request))
    {
    case usb::StandardRequest::GetStatus:
    {
        uint16_t status = 0;
        control_buffer_[0] = static_cast<uint8_t>(status & 0xFF);
        control_buffer_[1] = static_cast<uint8_t>((status >> 8) & 0xFF);
        SubmitDataIn(control_buffer_, 2);
        break;
    }
    case usb::StandardRequest::ClearFeature:
    {
        if (setup.Recipient() == usb::kRecipientEndpoint && setup.w_value == static_cast<uint16_t>(usb::FeatureSelector::EndpointHalt)) {
            hal_->EpStall(setup.w_index & 0xFF, false);
        }
        SendStatusIn();
        break;
    }
    case usb::StandardRequest::SetFeature:
    {
        if (setup.Recipient() == usb::kRecipientEndpoint && setup.w_value == static_cast<uint16_t>(usb::FeatureSelector::EndpointHalt)) {
            hal_->EpStall(setup.w_index & 0xFF, true);
            SendStatusIn();
        } else {
            hal_->EpStall(0x00, true);
        }
        break;
    }
    case usb::StandardRequest::SetAddress:
    {
        uint8_t addr = setup.w_value & 0xFF;
        hal_->SetAddress(addr);
        address_ = addr;
        state_   = usb::DeviceState::Addressed;
        SendStatusIn();
        break;
    }
    case usb::StandardRequest::GetDescriptor:
    {
        if (!SendDescriptor(setup)) hal_->EpStall(0x00, true);
        break;
    }
    case usb::StandardRequest::SetConfiguration:
    {
        uint8_t cfg_val = setup.w_value & 0xFF;
        if (cfg_val > 1) { 
            hal_->EpStall(0x00, true); 
            break; 
        }
        configuration_ = cfg_val;
        if (configuration_ != 0) 
        {
            if (!function_->OnConfigured(true)) {
                configuration_ = 0;
                state_ = usb::DeviceState::Addressed;
                hal_->EpStall(0x00, true);
                break;
            }
            state_ = usb::DeviceState::Configured;
        } 
        else {
            state_ = usb::DeviceState::Addressed;
            function_->OnConfigured(false);
        }
        SendStatusIn();

        break;
    }
    case usb::StandardRequest::GetConfiguration:
    {
        control_buffer_[0] = configuration_;
        SubmitDataIn(control_buffer_, 1);
        break;
    }
    case usb::StandardRequest::GetInterface:
    {
        control_buffer_[0] = 0;
        SubmitDataIn(control_buffer_, 1);
        break;
    }
    case usb::StandardRequest::SetInterface:
    {
        if (setup.w_value == 0) { SendStatusIn(); } else { hal_->EpStall(0x00, true); }
        break;
    }
    default:
        hal_->EpStall(0x00, true);
        break;
    }
}

//  —————————————————————————— Class 请求 ——————————————————————————

/**
 * @brief 将 Class 请求分发给功能类
 */
void UsbDevice::HandleClassRequest(const usb::SetupPacket& setup)
{
    if (function_ == nullptr) { 
        hal_->EpStall(0x00, true); 
        return; 
    }

    class_setup_ = setup;
    uint16_t len = setup.w_length;
    if (len > sizeof(control_buffer_)) len = sizeof(control_buffer_);

    if (function_->HandleClassRequest(setup, control_buffer_, len)) 
    {
        if (setup.IsDeviceToHost()) {
            SubmitDataIn(control_buffer_, len);
        } else if (len > 0) {
            SubmitDataOut(control_buffer_, len);
        } else {
            SendStatusIn();
        }
    } else {
        hal_->EpStall(0x00, true);
    }
}

//  —————————————————————————— 描述符查询 ——————————————————————————

/**
 * @brief 发送描述符（按 wLength 截断后通过 EP0 IN 返回）
 */
bool UsbDevice::SendDescriptor(const usb::SetupPacket& setup)
{
    uint8_t             desc_type   = (setup.w_value >> 8) & 0xFF;
    uint8_t             desc_index  = setup.w_value & 0xFF;
    usb::DescriptorType type        = static_cast<usb::DescriptorType>(desc_type);
    uint16_t            length      = 0;
    const uint8_t*      data        = function_->GetDescriptor(type, speed_, desc_index, length);

    if (data == nullptr || length == 0) return false;
    if (length > setup.w_length) length = setup.w_length;

    ctrl_.stage = Ep0Stage::DataIn;
    return hal_->Ep0StartIn(data, length);
}

//  —————————————————————————— EP0 统一提交 ——————————————————————————

/**
 * @brief EP0 DATA IN，失败时 STALL
 */
bool UsbDevice::SubmitDataIn(const uint8_t* data, uint16_t len)
{
    ctrl_.stage = Ep0Stage::DataIn;

    if (!hal_->Ep0StartIn(data, len)) 
    {
        LOG_ERR("SubmitDataIn(len=%u) failed", len);
        ctrl_.stage = Ep0Stage::Idle;
        hal_->EpStall(0x00, true);
        return false;
    }
    return true;
}

/**
 * @brief EP0 DATA OUT，失败时 STALL
 */
bool UsbDevice::SubmitDataOut(uint8_t* data, uint16_t len)
{
    ctrl_.stage = Ep0Stage::DataOut;

    if (!hal_->Ep0StartOut(data, len)) 
    {
        LOG_ERR("SubmitDataOut(len=%u) failed", len);
        ctrl_.stage = Ep0Stage::Idle;
        hal_->EpStall(0x00, true);
        return false;
    }
    return true;
}

/**
 * @brief EP0 STATUS IN，失败时 STALL
 */
bool UsbDevice::SubmitStatusIn()
{
    ctrl_.stage = Ep0Stage::StatusIn;

    if (!hal_->Ep0StatusIn()) 
    {
        LOG_ERR("SubmitStatusIn failed");
        ctrl_.stage = Ep0Stage::Idle;
        hal_->EpStall(0x00, true);
        return false;
    }
    return true;
}

/**
 * @brief EP0 STATUS OUT，失败时 STALL
 */
bool UsbDevice::SubmitStatusOut()
{
    ctrl_.stage = Ep0Stage::StatusOut;

    if (!hal_->Ep0StatusOut()) 
    {
        LOG_ERR("SubmitStatusOut failed");
        ctrl_.stage = Ep0Stage::Idle;
        hal_->EpStall(0x00, true);
        return false;
    }
    return true;
}

//  —————————————————————————— 传输完成 ——————————————————————————

/**
 * @brief 端点传输完成 — EP0 状态机推进/非 EP0 分发功能类
 */
void UsbDevice::HandleTransferComplete(const UsbHal::Event& event)
{
    if (event.endpoint == usb::kEpDirOut || event.endpoint == usb::kEpDirIn) 
    {
        switch (ctrl_.stage)
        {
            case Ep0Stage::DataIn:
            {
                SubmitStatusOut();
                break;
            }
            case Ep0Stage::DataOut:
            {
                if (event.error || event.data == nullptr || event.length == 0) {
                    ctrl_.stage = Ep0Stage::Idle;
                    hal_->EpStall(0x00, true);
                    break;
                }
                if (function_ && !function_->CompleteControlOut(class_setup_, event.data, event.length)) {
                    ctrl_.stage = Ep0Stage::Idle;
                    hal_->EpStall(0x00, true);
                    break;
                }
                SubmitStatusIn();
                break;
            }
            case Ep0Stage::StatusIn:
            case Ep0Stage::StatusOut:
            {
                ctrl_.stage = Ep0Stage::Idle;
                break;
            }
            default:
                break;
        }
    } else {
        if (function_) {
            function_->OnEndpointComplete(event.endpoint, event.data, event.length, event.error);
        }
    }
}

