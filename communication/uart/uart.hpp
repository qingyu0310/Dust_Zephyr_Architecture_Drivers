/**
 * @file uart.hpp
 * @author qingyu
 * @brief
 * @version 0.1
 * @date 2026-04-26
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "zephyr/device.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

class Uart final
{
public:
    bool Init(const struct device *dev)
    {
        dev_ = dev;
        if (!device_is_ready(dev_)) {
            printk("Error: UART device %s is not ready\n", dev_->name);
            return false;
        }
        uart_irq_callback_user_data_set(dev_, irq_handler, this);
        uart_irq_rx_enable(dev_);
        return true;
    }

    bool Send(const uint8_t *data, uint32_t len)
    {
        for (uint32_t i = 0; i < len; i++) {
            uart_poll_out(dev_, data[i]);
        }
        return true;
    }

    int Read(uint8_t *buf, uint32_t len)
    {
        uint32_t i = 0;
        while (i < len && rx_head_ != rx_tail_) {
            buf[i++] = rx_buf_[rx_tail_];
            rx_tail_ = (rx_tail_ + 1) % RX_BUF_SIZE;
        }
        return i;
    }

private:
    static constexpr uint32_t RX_BUF_SIZE = 256;

    static void irq_handler(const struct device *dev, void *user_data)
    {
        auto *self = static_cast<Uart *>(user_data);
        if (!uart_irq_rx_ready(dev)) return;

        uint8_t byte;
        while (uart_fifo_read(dev, &byte, 1) > 0) {
            uint32_t next = (self->rx_head_ + 1) % RX_BUF_SIZE;
            if (next != self->rx_tail_) {
                self->rx_buf_[self->rx_head_] = byte;
                self->rx_head_ = next;
            }
        }
    }

    const struct device *dev_{};
    uint8_t rx_buf_[RX_BUF_SIZE]{};
    volatile uint32_t rx_head_ = 0;
    volatile uint32_t rx_tail_ = 0;
};

