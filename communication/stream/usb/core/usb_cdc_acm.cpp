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

static constexpr uint8_t  kCdcReqSetLineCoding        = 0x20;    // 设置线编码
static constexpr uint8_t  kCdcReqGetLineCoding        = 0x21;    // 获取线编码
static constexpr uint8_t  kCdcReqSetControlLineState  = 0x22;    // 设置控制线状态
static constexpr uint8_t  kCdcReqSendBreak            = 0x23;    // 发送 BREAK
static constexpr uint16_t kLineCodingLen              = 7;       // 线编码固定长度

// bmRequestType 组合值：H2D + Class + Interface
static constexpr uint8_t kCdcBmHostToDev = usb::kDirectionHostToDevice | usb::kTypeClass | usb::kRecipientInterface;
// bmRequestType 组合值：D2H + Class + Interface
static constexpr uint8_t kCdcBmDevToHost = usb::kDirectionDeviceToHost | usb::kTypeClass | usb::kRecipientInterface;

/**
 * @brief 初始化
 * @param device  UsbDevice 引用
 * @param cfg     CDC 配置（外部持有，Init 不复制）
 * @return true=成功
 */
bool UsbCdcAcm::Init(UsbDevice& device, const UsbCdcAcmConfig& cfg)
{
    if (ready_) {
        return true;
    }

    device_ = &device;
    cfg_    = &cfg;

    // 创建描述符集
    UsbDescriptorSet::Config desc_cfg {};
    desc_cfg.vid              = cfg.vid;
    desc_cfg.pid              = cfg.pid;
    desc_cfg.bcd_device       = cfg.bcd_device;
    desc_cfg.control_interface= cfg.control_interface;
    desc_cfg.data_interface   = cfg.data_interface;
    desc_cfg.notification_ep  = cfg.notification_ep;
    desc_cfg.bulk_out_addr    = cfg.bulk_out_addr;
    desc_cfg.bulk_in_addr     = cfg.bulk_in_addr;
    desc_cfg.manufacturer     = cfg.manufacturer;
    desc_cfg.product          = cfg.product;
    desc_cfg.serial_number    = cfg.serial_number;

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

//  —————————————————————————— 配置状态 ——————————————————————————

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

        UsbHal& hal = device_->GetHal();

        usb::EndpointConfig int_ep {cfg_->notification_ep, usb::EndpointType::Interrupt,  8, 16};
        usb::EndpointConfig out_ep {cfg_->bulk_out_addr,   usb::EndpointType::Bulk,     mps, 0};
        usb::EndpointConfig in_ep  {cfg_->bulk_in_addr,    usb::EndpointType::Bulk,     mps, 0};

        bool ok = true;
        ok &= hal.EpOpen(int_ep);
        ok &= hal.EpOpen(out_ep);
        ok &= hal.EpOpen(in_ep);

        if (ok) 
        {
            if (!hal.EpStartRx(out_ep.address, out_ep.max_packet_size)) {
                LOG_ERR("First EpStartRx failed");
                ok = false;
            }
        } else {
            LOG_ERR("EpOpen failed, endpoints not ready");
        }

        if (!ok) {
            hal.EpClose(int_ep.address);
            hal.EpClose(out_ep.address);
            hal.EpClose(in_ep.address);
            configured_ = false;
            if (cfg_cb_) cfg_cb_(cfg_ctx_, false, 0);
            return false;
        }

        configured_ = true;
        LOG_INF("CDC configured, speed=%s MPS=%u", (speed == usb::Speed::High) ? "HS" : "FS", bulk_mps_);
        if (cfg_cb_) cfg_cb_(cfg_ctx_, true, bulk_mps_);

        return true;
    } 
    else 
    {
        if (configured_) {
            UsbHal& hal = device_->GetHal();
            hal.EpClose(cfg_->notification_ep);
            hal.EpClose(cfg_->bulk_out_addr);
            hal.EpClose(cfg_->bulk_in_addr);
        }
        configured_  = false;
        dtr_         = false;
        rts_         = false;
        notify_busy_ = false;

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
    if (error) 
    {
        LOG_ERR("RX error on ep 0x%02x, trying recovery", endpoint);

        if (endpoint == cfg_->bulk_out_addr && configured_) 
        {
            UsbHal& hal = device_->GetHal();
            hal.EpStall(endpoint, false);
            hal.EpClose(endpoint);
            usb::EndpointConfig ep_cfg {cfg_->bulk_out_addr, usb::EndpointType::Bulk, bulk_mps_, 0};
            hal.EpOpen(ep_cfg);

            if (!hal.EpStartRx(endpoint, bulk_mps_)) {
                LOG_ERR("RX recovery failed, RX stopped");
            } else {
                LOG_INF("RX recovered on ep 0x%02x", endpoint);
            }
        }
        return;
    }

    if (data_cb_ != nullptr) {
        data_cb_(data_ctx_, endpoint, data, length);
    }

    if (endpoint == cfg_->bulk_out_addr && configured_) 
    {
        UsbHal& hal = device_->GetHal();
        if (!hal.EpStartRx(cfg_->bulk_out_addr, bulk_mps_)) {
            LOG_ERR("RX re-submit failed, RX stopped");
        }
    }
}

//  —————————————————————————— CDC 类请求处理 ——————————————————————————

/**
 * @brief 处理 CDC 类请求
 */
bool UsbCdcAcm::HandleClassRequest(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
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
 */
bool UsbCdcAcm::OnSetLineCoding(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
{
    if (setup.bm_request_type != kCdcBmHostToDev) return false;
    if (setup.w_index != cfg_->control_interface) return false;
    if (setup.w_length != kLineCodingLen) return false;
    length = kLineCodingLen;

    return true;
}

/**
 * @brief 处理 GET_LINE_CODING 请求
 */
bool UsbCdcAcm::OnGetLineCoding(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
{
    if (setup.bm_request_type != kCdcBmDevToHost) return false;
    if (setup.w_index != cfg_->control_interface) return false;

    usb::EncodeLineCoding(line_coding_, data);
    length = kLineCodingLen;

    return true;
}

/**
 * @brief 处理 SET_CONTROL_LINE_STATE 请求
 */
bool UsbCdcAcm::OnSetControlLineState(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
{
    (void)data;
    if (setup.bm_request_type != kCdcBmHostToDev) return false;
    if (setup.w_index != cfg_->control_interface) return false;
    dtr_ = (setup.w_value & 0x01) != 0;
    rts_ = (setup.w_value & 0x02) != 0;
    LOG_INF("DTR=%d RTS=%d", dtr_, rts_);
    length = 0;
    return true;
}

/**
 * @brief 处理 SEND_BREAK 请求
 */
bool UsbCdcAcm::OnSendBreak(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length)
{
    (void)data;
    if (setup.bm_request_type != kCdcBmHostToDev) return false;
    if (setup.w_index != cfg_->control_interface) return false;
    break_value_ = setup.w_value;
    LOG_INF("BREAK value=%u", break_value_);
    length = 0;
    return true;
}

/**
 * @brief EP0 DATA OUT 完成（冷路径：SET_LINE_CODING 将数据写入线编码）
 */
bool UsbCdcAcm::CompleteControlOut(const usb::SetupPacket& setup, const uint8_t* data, uint16_t length)
{
    if (setup.b_request == kCdcReqSetLineCoding && length >= kLineCodingLen) {
        usb::DecodeLineCoding(data, line_coding_);
        LOG_INF("line_coding updated: rate=%u", line_coding_.dte_rate);
        return true;
    }
    return false;
}

//  —————————————————————————— 描述符查询 ——————————————————————————

/**
 * @brief 获取描述符
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
