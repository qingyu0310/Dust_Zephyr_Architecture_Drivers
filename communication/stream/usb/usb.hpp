/**
 * @file usb.hpp
 * @author qingyu
 * @brief USB CDC ACM 顶层封装 — 继承 Stream，串联 UsbCdcAcm + UsbDevice + UsbHal
 * @version 0.4
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

#include "stream.hpp"

// USB 协议栈（用户自维护库，D:/Zephyr/modules/user/usb 已加入 include 路径）
#include "usb_cdc_config.hpp"
#include "usb_cdc_acm.hpp"
#include "usb_device.hpp"
#include "usb_rx_queue.hpp"

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
    struct Config {
        uint8_t  busid         = 0;                     // USB 总线号
        uint32_t reg_base      = 0;                     // 控制器寄存器基址
        uint32_t irq_num       = 0;                     // 中断号
        uint32_t irq_priority  = 1;                     // PLIC 中断优先级（必须 >0）
    };

    bool     Init(const Config& cfg);
    bool     IsReady()  const { return ready_; }
    bool     IsTxBusy() const { return tx_busy_; }

    uint16_t Read(uint8_t* buf, uint16_t max_len) override { return rx_queue_.Pop(buf, max_len); }
    bool     Send(const uint8_t* data, uint32_t len) override;

private:
    static constexpr uint16_t kMaxBufSize = 512;

    UsbCdcAcm  cdc_    {};                                  // CDC ACM 协议
    UsbCdcAcmConfig cdc_cfg_ {};                            // CDC 配置
    UsbDevice  device_ {};                                  // USB 设备核心
    UsbHal*    hal_     = nullptr;                          // 硬件抽象层

    atomic_t   ready_      = ATOMIC_INIT(0);                // Init 完成
    atomic_t   tx_busy_    = ATOMIC_INIT(0);                // 发送中
    atomic_t   configured_ = ATOMIC_INIT(0);                // 已配置
    uint16_t   bulk_mps_   = 64;                            // 批量端点 MPS

    UsbRxQueue rx_queue_ {};                                // 接收队列
    k_spinlock lock_{};                                     // 并发锁

    // 从 CDC 配置查询 bulk 端点地址
    uint8_t     GetBulkOutEp() const { return cdc_cfg_.bulk_out_addr; }
    uint8_t     GetBulkInEp()  const { return cdc_cfg_.bulk_in_addr; }

    static void OnDataEvent(void* ctx, uint8_t ep, const uint8_t* data, uint16_t len);
    void        OnBulkOut(const uint8_t* data, uint16_t len) { rx_queue_.Push(data, len); }
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
