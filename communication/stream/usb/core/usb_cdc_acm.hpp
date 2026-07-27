/**
 * @file usb_cdc_acm.hpp
 * @author qingyu
 * @brief CDC ACM 协议实现 — line coding / DTR/RTS / BREAK / notification
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

#include "usb_device.hpp"
#include "usb_descriptor.hpp"

/**
 * @brief CDC ACM 协议处理器
 *
 * 职责：
 *   - CDC 类请求（SET/GET_LINE_CODING、SET_CONTROL_LINE_STATE、SEND_BREAK）
 *   - Interrupt IN notification（串口状态通知）
 *   - bulk OUT/IN 完成转发到 OnEndpointComplete
 *
 * 收发缓冲和 Stream 接口在顶层 Usb 中管理，不在此类。
 */
class UsbCdcAcm final : public UsbFunction
{
public:
    /**
     * @brief CDC ACM 配置
     */
    struct Config {
        usb::EndpointConfig int_ep {0x83, usb::EndpointType::Interrupt, 8,  16};
        usb::EndpointConfig out_ep {0x01, usb::EndpointType::Bulk,      64, 0};
        usb::EndpointConfig in_ep  {0x81, usb::EndpointType::Bulk,      64, 0};
        uint8_t  control_interface = 0;         // CDC 控制接口号
        uint8_t  data_interface    = 1;         // CDC 数据接口号
        bool     require_dtr       = false;     // 是否等待 DTR 才允许发送
    };

    bool Init(UsbDevice& device, const Config& cfg);

    /**
     * @brief 数据事件回调类型
     */
    using DataCallback = void (*)(void* ctx, uint8_t ep, const uint8_t* data, uint16_t len);
    using ConfigureCallback = void (*)(void* ctx, bool configured, uint16_t bulk_mps);
    void SetDataCallback(DataCallback cb, void* ctx);
    void SetConfigureCallback(ConfigureCallback cb, void* ctx);
    const uint8_t* GetDescriptor(usb::DescriptorType type, usb::Speed speed, uint8_t index, uint16_t& length) const override;
    bool HandleClassRequest(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length) override;
    bool OnConfigured(bool configured) override;
    void OnEndpointComplete(uint8_t endpoint, const uint8_t* data, uint16_t length, bool error = false) override;
    bool CompleteControlOut(const usb::SetupPacket& setup, const uint8_t* data, uint16_t length) override;

private:
    static constexpr uint16_t kMaxBufSize = 512;

    // CDC 请求处理
    bool OnSetLineCoding(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length);
    bool OnGetLineCoding(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length);
    bool OnSetControlLineState(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length);
    bool OnSendBreak(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length);

    // 引用
    UsbDevice*  device_ = nullptr;

    // 描述符集合
    UsbDescriptorSet* desc_set_ = nullptr;

    // 配置
    Config      cfg_        {};
    uint16_t    bulk_mps_   = 64;

    // CDC 状态
    usb::LineCoding line_coding_ {};
    bool dtr_          = false;
    bool rts_          = false;
    uint16_t break_value_ = 0;

    // 设备状态
    bool ready_      = false;
    bool configured_ = false;

    // 数据事件回调（顶层 Usb 注册，用于收发）
    DataCallback data_cb_  = nullptr;
    void*        data_ctx_ = nullptr;

    // 配置状态回调（顶层 Usb 注册）
    ConfigureCallback cfg_cb_ = nullptr;
    void*             cfg_ctx_ = nullptr;

    // Interrupt IN notification
    uint8_t  notify_buf_[10] {};
    bool     notify_busy_ = false;
};
