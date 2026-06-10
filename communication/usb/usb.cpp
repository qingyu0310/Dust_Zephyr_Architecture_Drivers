/**
 * @file usb.cpp
 * @author qingyu
 * @brief USB CDC ACM driver implementation.
 * @version 0.1
 * @date 2026-06-09
 */

#include "usb.hpp"

#ifdef CONFIG_COM_USB

void usb_uart_irq_handler(const struct device* dev, void* user_data)
{
    auto* self = static_cast<UsbUart*>(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev))
    {
        if (!uart_irq_rx_ready(dev)) {
            continue;
        }

        uint8_t byte = 0;
        while (uart_fifo_read(dev, &byte, 1) > 0)
        {
            uint16_t next = (self->head_ + 1) % self->buf_size_;
            if (next != self->tail_) {
                self->rx_buf_[self->head_] = byte;
                self->head_ = next;
            }
        }

        if (self->notify_sem_) {
            k_sem_give(self->notify_sem_);
        }
    }
}

bool UsbUart::Init(const struct device* dev, const RxStream::Config& cfg)
{
    Config usb_cfg {};
    usb_cfg.buf_size   = cfg.buf_size;
    usb_cfg.rx_timeout = cfg.rx_timeout;
    return Init(dev, usb_cfg);
}

bool UsbUart::Init(const struct device* dev, const Config& cfg)
{
    if (dev == nullptr) {
        return false;
    }

    dev_        = dev;
    head_       = 0;
    tail_       = 0;
    ready_      = false;
    notify_sem_ = nullptr;

    if (!device_is_ready(dev_)) {
        int ret = device_init(dev_);
        if (ret != 0 || !device_is_ready(dev_)) {
            return false;
        }
    }

    uint16_t bs = cfg.buf_size;
    if (bs < 2 || bs > kMaxBufSize) {
        bs = kMaxBufSize;
    }
    buf_size_ = bs;

    if (cfg.wait_dtr && !WaitHostReady(cfg.dtr_timeout_ms)) {
        return false;
    }

    if (cfg.assert_line_state) {
        (void)uart_line_ctrl_set(dev_, UART_LINE_CTRL_DCD, 1);
        (void)uart_line_ctrl_set(dev_, UART_LINE_CTRL_DSR, 1);
        k_msleep(100);
    }

    uart_irq_callback_user_data_set(dev_, usb_uart_irq_handler, this);
    uart_irq_rx_enable(dev_);
    ready_ = true;
    return true;
}

void UsbUart::SetNotify(struct k_sem* sem)
{
    notify_sem_ = sem;
}

uint16_t UsbUart::Read(uint8_t* buf, uint16_t max_len)
{
    uint16_t cnt = 0;
    while (cnt < max_len && tail_ != head_) {
        buf[cnt++] = rx_buf_[tail_];
        tail_ = (tail_ + 1) % buf_size_;
    }
    return cnt;
}

bool UsbUart::Send(const uint8_t* data, uint32_t len) const
{
    if (!ready_) return false;
    if (data == nullptr || len == 0) return false;

    for (uint32_t i = 0; i < len; i++) {
        uart_poll_out(dev_, data[i]);
    }
    return true;
}

bool UsbUart::WaitHostReady(int32_t timeout_ms) const
{
    if (dev_ == nullptr) {
        return false;
    }

    int64_t start_ms = k_uptime_get();
    while (true)
    {
        uint32_t dtr = 0;
        if (uart_line_ctrl_get(dev_, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U) {
            return true;
        }

        if (timeout_ms > 0 && (k_uptime_get() - start_ms) >= timeout_ms) {
            return false;
        }

        k_msleep(20);
    }
}

#endif // CONFIG_COM_USB
