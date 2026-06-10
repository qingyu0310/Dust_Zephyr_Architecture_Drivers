/**
 * @file usb.hpp
 * @author qingyu
 * @brief USB CDC ACM driver based on Zephyr USB device stack.
 * @version 0.1
 * @date 2026-06-09
 *
 * @code{.cpp}
 * #define PC_USB_NODE DT_ALIAS(pc_usb)
 *
 * static UsbUart usb {};
 * static K_SEM_DEFINE(usb_rx_sem, 0, 1);
 *
 * void usb_init()
 * {
 *     UsbUart::Config cfg {};
 *     cfg.buf_size       = 128;
 *     cfg.wait_dtr       = true;
 *     cfg.dtr_timeout_ms = 3000;
 *
 *     if (!usb.Init(DEVICE_DT_GET(PC_USB_NODE), cfg)) {
 *         return;
 *     }
 *
 *     usb.SetNotify(&usb_rx_sem);
 * }
 *
 * void usb_task()
 * {
 *     uint8_t rx[64];
 *
 *     while (true) {
 *         if (k_sem_take(&usb_rx_sem, K_FOREVER) != 0) {
 *             continue;
 *         }
 *
 *         uint16_t len = usb.Read(rx, sizeof(rx));
 *         if (len > 0) {
 *             usb.Send(rx, len);
 *         }
 *     }
 * }
 * @endcode
 */

#pragma once

#include "uart.hpp"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

class UsbUart final : public RxStream
{
    friend void usb_uart_irq_handler(const struct device* dev, void* user_data);

public:
    struct Config : public RxStream::Config {
        bool    wait_dtr          = true;
        int32_t dtr_timeout_ms    = 0;
        bool    assert_line_state = true;
    };

    bool     Init(const struct device* dev, const RxStream::Config& cfg) override;
    bool     Init(const struct device* dev, const Config& cfg);
    void     SetNotify(struct k_sem* sem) override;
    uint16_t Read(uint8_t* buf, uint16_t max_len) override;
    bool     Send(const uint8_t* data, uint32_t len) const;
    bool     WaitHostReady(int32_t timeout_ms = 0) const;

    bool IsReady() const { return ready_; }

private:
    static constexpr uint16_t kMaxBufSize = 512;

    const struct device* dev_      = nullptr;
    uint8_t              rx_buf_[kMaxBufSize] {};
    uint16_t             buf_size_ = 128;
    uint16_t             head_     = 0;
    uint16_t             tail_     = 0;
    bool                 ready_    = false;
    k_sem*               notify_sem_ = nullptr;
};
