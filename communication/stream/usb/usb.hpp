/**
 * @file usb.hpp
 * @author qingyu
 * @brief USB CDC ACM 顶层封装 — 继承 Stream，串联 UsbDevPort + UsbHal
 * @version 0.4
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>
#include "stream.hpp"
#include "usb_cdc_config.hpp"
#include "usb_dev_port.hpp"
#include "usb_rx_queue.hpp"
#include "usb_hal.hpp"

/**
 * @brief USB CDC ACM 顶层封装
 *
 * 继承 Stream，对外提供统一的 Read/Send 接口。
 * DMA 缓冲由 UsbHal 管理，本层只维护软件接收队列。
 */
namespace usb {

class Usb final : public Stream
{
public:
	static constexpr uint16_t kTxBufSize = UsbHal::kTxBufSize;
    static constexpr uint16_t kRxBufSize = UsbHal::kRxBufSize;

    bool     Init(const UsbHal::Config& cfg, const UsbCdcAcmConfig& cdc_cfg = UsbCdcAcmConfig{});
    bool     IsReady()  const { return ready_; }
    bool     IsTxBusy() const { return tx_busy_; }

    uint16_t Read(uint8_t* buf, uint16_t max_len)    override { return rx_queue_.Pop(buf, max_len); }
    bool     Send(const uint8_t* data, uint32_t len) override;

private:
    UsbDevPort device_      {};                     // USB 设备核心

    atomic_t   ready_        = ATOMIC_INIT(0);      // Init 完成
    atomic_t   tx_busy_      = ATOMIC_INIT(0);      // 发送中
    atomic_t   configured_   = ATOMIC_INIT(0);      // 已配置
    uint16_t   bulk_mps_     = 64;                  // 批量端点 MPS
    k_spinlock lock_        {};                     // 并发锁

    UsbRxQueue<kRxBufSize> rx_queue_ {};            // 接收队列
    
    // 从 CDC 配置查询 bulk 端点地址
    uint8_t     GetBulkOutEp() { return device_.GetCdcAcm().GetBulkOutEp(); }
    uint8_t     GetBulkInEp()  { return device_.GetCdcAcm().GetBulkInEp(); }

    static void OnDataEvent(void* ctx, uint8_t ep, const uint8_t* data, uint16_t len);
    void        OnBulkIn(uint16_t len);
    void        OnConfigured(bool configured, uint16_t bulk_mps);

    static void OnConfigureEvent(void* ctx, bool configured, uint16_t bulk_mps)
    {   
        if (ctx) {
            static_cast<Usb*>(ctx)->OnConfigured(configured, bulk_mps); 
        }
    }
};

} // namespace usb
