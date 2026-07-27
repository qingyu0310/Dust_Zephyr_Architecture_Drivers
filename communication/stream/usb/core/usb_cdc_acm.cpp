/**
 * @file usb_cdc_acm.cpp
 * @author qingyu
 * @brief CDC ACM 协议实现
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#include "usb_cdc_acm.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_cdc_acm, LOG_LEVEL_INF);

static constexpr uint8_t  kCdcReqSetLineCoding        = 0x20;    // SET_LINE_CODING
static constexpr uint8_t  kCdcReqGetLineCoding        = 0x21;    // GET_LINE_CODING
static constexpr uint8_t  kCdcReqSetControlLineState  = 0x22;    // SET_CONTROL_LINE_STATE
static constexpr uint8_t  kCdcReqSendBreak            = 0x23;    // SEND_BREAK
static constexpr uint16_t kLineCodingLen              = 7;       // 线编码固定长度

/**
 * @brief 初始化
 * @param device  UsbDevice 引用
 * @param cfg     配置
 * @return true=成功
 */
bool UsbCdcAcm::Init(UsbDevice& device, const Config& cfg)
{
    static constexpr uint16_t kUsbVid = 0x34B7;     // USB 厂商 ID
    static constexpr uint16_t kUsbPid = 0xFFFF;     // USB 产品 ID

    if (ready_) {
        return true;
    }

    device_ = &device;
    cfg_    = cfg;

    // 创建描述符集
    UsbDescriptorSet::Config desc_cfg {};
    desc_cfg.vid = kUsbVid;
    desc_cfg.pid = kUsbPid;

    static UsbDescriptorSet s_desc_set(desc_cfg);
    desc_set_ = &s_desc_set;

    // 初始化线编码
    line_coding_.dte_rate    = 115200;
    line_coding_.char_format = 0;
    line_coding_.parity_type = 0;
    line_coding_.data_bits   = 8;

    ready_ = true;
    return true;
}

/**
 * @brief 注册数据事件回调
 */
void UsbCdcAcm::SetDataCallback(DataCallback cb, void* ctx)
{
    data_cb_  = cb;
    data_ctx_ = ctx;
}

/**
 * @brief 注册配置状态变化回调
 */
void UsbCdcAcm::SetConfigureCallback(ConfigureCallback cb, void* ctx)
{
    cfg_cb_  = cb;
    cfg_ctx_ = ctx;
}

/**
 * @brief 获取描述符
 * @param type    描述符类型
 * @param speed   当前 USB 速度
 * @param index   描述符索引（字符串/配置）
 * @param length  输出：描述符长度
 * @return 描述符数据指针，失败返回 nullptr
 */
const uint8_t* UsbCdcAcm::GetDescriptor(usb::DescriptorType type, usb::Speed speed, uint8_t index, uint16_t& length) const
{
    if (desc_set_ == nullptr) {
        length = 0;
        return nullptr;
    }

    switch (type)
    {
        case usb::DescriptorType::Device:
            return desc_set_->GetDeviceDescriptor(length);
        case usb::DescriptorType::Configuration:
            return desc_set_->GetConfigurationDescriptor(speed, length);
        case usb::DescriptorType::DeviceQualifier:
            return desc_set_->GetQualifierDescriptor(length);
        case usb::DescriptorType::OtherSpeedConfiguration:
            return desc_set_->GetOtherSpeedDescriptor(speed, length);
        case usb::DescriptorType::String:
            return desc_set_->GetStringDescriptor(index, length);
        default:
            length = 0;
            return nullptr;
    }
}

// CDC 类请求

/**
 * @brief 处理 CDC 类请求
 * @param setup  SETUP 包
 * @param data   数据缓冲（DATA OUT 时有效）
 * @param length 输入输出：数据长度
 * @return true=已处理，false=不支持
 */
bool UsbCdcAcm::HandleClassRequest(const usb::SetupPacket& setup,
                                    uint8_t* data, uint16_t& length)
{
    switch (setup.b_request)
    {
        case kCdcReqSetLineCoding:
            return OnSetLineCoding(setup, data, length);
        case kCdcReqGetLineCoding:
            return OnGetLineCoding(setup, data, length);
        case kCdcReqSetControlLineState:
            return OnSetControlLineState(setup, data, length);
        case kCdcReqSendBreak:
            return OnSendBreak(setup, data, length);
        default:
            return false;
    }
}

/**
 * @brief 处理 SET_LINE_CODING 请求
 * @param setup  SETUP 包
 * @param data   数据缓冲
 * @param length 数据长度
 * @return true=已接受
 */
bool UsbCdcAcm::OnSetLineCoding(const usb::SetupPacket& setup,
                                 uint8_t* data, uint16_t& length)
{
    if (setup.bm_request_type != 0x21) return false;
    if (setup.w_index != cfg_.control_interface) return false;
    if (setup.w_length != kLineCodingLen) return false;

    length = kLineCodingLen;
    return true;
}

/**
 * @brief 处理 GET_LINE_CODING 请求
 * @param setup  SETUP 包
 * @param data   输出缓冲（编码 7 字节线编码）
 * @param length 数据长度
 * @return true=已处理
 */
bool UsbCdcAcm::OnGetLineCoding(const usb::SetupPacket& setup,
                                 uint8_t* data, uint16_t& length)
{
    if (setup.bm_request_type != 0xA1) return false;
    if (setup.w_index != cfg_.control_interface) return false;

    usb::EncodeLineCoding(line_coding_, data);
    length = kLineCodingLen;
    return true;
}

/**
 * @brief 处理 SET_CONTROL_LINE_STATE 请求
 * @param setup  SETUP 包（wValue bit0=DTR, bit1=RTS）
 * @param data   数据缓冲（无数据）
 * @param length 数据长度（输出 0）
 * @return true=已处理
 */
bool UsbCdcAcm::OnSetControlLineState(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
{
    (void)data;

    if (setup.bm_request_type != 0x21) return false;
    if (setup.w_index != cfg_.control_interface) return false;

    dtr_ = (setup.w_value & 0x01) != 0;
    rts_ = (setup.w_value & 0x02) != 0;
    LOG_DBG("DTR=%d RTS=%d", dtr_, rts_);

    length = 0;
    return true;
}

/**
 * @brief 处理 SEND_BREAK 请求
 * @param setup  SETUP 包（wValue 为 break 时长）
 * @param data   数据缓冲（无数据）
 * @param length 数据长度（输出 0）
 * @return true=已处理
 */
bool UsbCdcAcm::OnSendBreak(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
{
    (void)data;

    if (setup.bm_request_type != 0x21) return false;
    if (setup.w_index != cfg_.control_interface) return false;

    break_value_ = setup.w_value;
    LOG_DBG("BREAK value=%u", break_value_);

    length = 0;
    return true;
}

/**
 * @brief 配置状态变化通知
 * @param configured  true=已配置，false=取消配置
 */
bool UsbCdcAcm::OnConfigured(bool configured)
{
    if (configured) {
        usb::Speed speed = device_->GetSpeed();
        uint16_t mps = (speed == usb::Speed::High) ? 512 : 64;
        bulk_mps_ = mps;

        // 将实际 MPS 同步到 Config，使 EpOpen/EpStartRx/ZLP 使用同一套值
        cfg_.out_ep.max_packet_size = mps;
        cfg_.in_ep.max_packet_size  = mps;

        UsbHal& hal = device_->GetHal();
        bool ok = true;
        ok &= hal.EpOpen(cfg_.int_ep);
        ok &= hal.EpOpen(cfg_.out_ep);
        ok &= hal.EpOpen(cfg_.in_ep);

        if (ok) {
            // OUT 端点接收已就绪，提交第一次接收
            if (!hal.EpStartRx(cfg_.out_ep.address, cfg_.out_ep.max_packet_size)) {
                LOG_ERR("First EpStartRx failed");
                ok = false;
            }
        } else {
            LOG_ERR("EpOpen failed, endpoints not ready");
        }

        if (!ok) {
            // 回滚已打开的端点
            hal.EpClose(cfg_.int_ep.address);
            hal.EpClose(cfg_.out_ep.address);
            hal.EpClose(cfg_.in_ep.address);
            configured_ = false;
            // 通知顶层配置失败
            if (cfg_cb_) cfg_cb_(cfg_ctx_, false, 0);
            return false;
        }

        configured_ = true;

        LOG_INF("CDC configured, speed=%s MPS=%u", (speed == usb::Speed::High) ? "HS" : "FS", bulk_mps_);

        // 通知顶层 Usb 配置状态
        if (cfg_cb_) cfg_cb_(cfg_ctx_, true, bulk_mps_);
        return true;
    } else {
        // 取消配置：关闭端点
        if (configured_) {
            UsbHal& hal = device_->GetHal();
            hal.EpClose(cfg_.out_ep.address);
            hal.EpClose(cfg_.in_ep.address);
            hal.EpClose(cfg_.int_ep.address);
        }

        configured_  = false;
        dtr_         = false;
        rts_         = false;
        notify_busy_ = false;

        // 通知顶层取消配置
        if (cfg_cb_) cfg_cb_(cfg_ctx_, false, 0);
    }

    return true;
}

/**
 * @brief 端点完成通知
 * @param endpoint  端点号
 * @param data      数据指针
 * @param length    数据长度
 * @param error     传输错误标志
 */
void UsbCdcAcm::OnEndpointComplete(uint8_t endpoint, const uint8_t* data, uint16_t length, bool error)
{
    // 错误时不通知 data_cb_（data/length 无效）
    // 尝试恢复：清 halt、重开端点、重新提交 RX
    if (error) {
        LOG_ERR("RX error on ep 0x%02x, trying recovery", endpoint);
        if (endpoint == cfg_.out_ep.address && configured_) {
            UsbHal& hal = device_->GetHal();
            // 先清 STALL/halt，再重新打开端点
            hal.EpStall(endpoint, false);
            hal.EpClose(endpoint);
            hal.EpOpen(cfg_.out_ep);
            if (!hal.EpStartRx(endpoint, cfg_.out_ep.max_packet_size)) {
                LOG_ERR("RX recovery failed, RX stopped");
            } else {
                LOG_INF("RX recovered on ep 0x%02x", endpoint);
            }
        }
        return;
    }

    // 通知上层处理数据
    if (data_cb_ != nullptr) {
        data_cb_(data_ctx_, endpoint, data, length);
    }

    // OUT 端点完成后重新提交 RX
    if (endpoint == cfg_.out_ep.address && configured_) {
        UsbHal& hal = device_->GetHal();
        if (!hal.EpStartRx(cfg_.out_ep.address, cfg_.out_ep.max_packet_size)) {
            LOG_ERR("RX re-submit failed, RX stopped");
        }
    }
}

/**
 * @brief EP0 DATA OUT 阶段完成
 *
 * SET_LINE_CODING 的 DATA OUT 完成后解码线编码并保存。
 */
bool UsbCdcAcm::CompleteControlOut(const usb::SetupPacket& setup,
                                    const uint8_t* data, uint16_t length)
{
    if (setup.b_request == kCdcReqSetLineCoding && length >= kLineCodingLen) {
        usb::DecodeLineCoding(data, line_coding_);
        LOG_DBG("line_coding updated: rate=%u", line_coding_.dte_rate);
        return true;
    }
    return false;
}
