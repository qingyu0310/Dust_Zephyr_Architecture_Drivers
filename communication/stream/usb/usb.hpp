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

#include "../stream.hpp"
#include "usb_cdc_acm.hpp"
#include "usb_device.hpp"
#include "bipbuf.hpp"

/**
 * @brief USB CDC ACM 顶层封装
 *
 * 继承 Stream，对外提供统一的 Read/Send 接口。
 * DMA 缓冲由 UsbHal 管理，本层只维护软件接收队列。
 */
class Usb final : public Stream
{
public:
    struct Config {
        UsbHal*  hal           = nullptr;   // 硬件抽象层
        uint8_t  busid         = 0;         // USB 总线号
        uint32_t reg_base      = 0;         // 控制器寄存器基址
        unsigned int irq_num       = 0;     // 中断号
        unsigned int irq_priority  = 1;     // PLIC 中断优先级（必须 >0）
    };

    bool     Init(const Config& cfg);
    bool     IsReady()  const { return ready_; }
    bool     IsTxBusy() const { return tx_busy_; }

    uint16_t Read(uint8_t* buf, uint16_t max_len) override;
    bool     Send(const uint8_t* data, uint32_t len) override;

private:
    static constexpr uint16_t kMaxBufSize = 512;

    static void OnDataEvent(void* ctx, uint8_t ep, const uint8_t* data, uint16_t len);
    void OnBulkOut(const uint8_t* data, uint16_t len);
    void OnBulkIn(uint16_t len);

    static void OnConfigureEvent(void* ctx, bool configured, uint16_t bulk_mps);
    void OnConfigured(bool configured, uint16_t bulk_mps);

    UsbDevice  device_ {};
    UsbCdcAcm  cdc_    {};
    UsbHal*    hal_    = nullptr;

    atomic_t   ready_      = ATOMIC_INIT(0);
    atomic_t   tx_busy_    = ATOMIC_INIT(0);
    atomic_t   configured_ = ATOMIC_INIT(0);
    uint16_t   bulk_mps_   = 64;

    BipBuffer<1024> rx_bip_ {};
    k_spinlock lock_{};
};
