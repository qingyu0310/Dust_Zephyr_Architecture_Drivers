/**
 * @file uart.cpp
 * @author qingyu
 * @brief UART 驱动实现
 * @version 0.4
 * @date 2026-05-16
 */

#include "uart.hpp"
#include <string.h>

#ifdef CONFIG_COM_UART

void uart_irq_handler(const struct device* dev, void* user_data)
{
    auto* self = static_cast<Uart*>(user_data);

    if (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        while (uart_fifo_read(dev, &byte, 1) > 0) {
            uint16_t next = (self->head_ + 1) % sizeof(self->rx_buf_);
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

bool Uart::Init(const struct device* dev, const Config& cfg)
{
    dev_ = dev;
    if (!device_is_ready(dev_)) {
        return false;
    }

    head_     = 0;
    tail_     = 0;
    buf_size_ = cfg.buf_size > kMaxBufSize ? kMaxBufSize : cfg.buf_size;

    uart_irq_callback_user_data_set(dev_, uart_irq_handler, this);
    uart_irq_rx_enable(dev_);
    return true;
}

void Uart::SetNotify(struct k_sem* sem)
{
    notify_sem_ = sem;
}

uint16_t Uart::Read(uint8_t* buf, uint16_t max_len)
{
    uint16_t cnt = 0;
    while (cnt < max_len && tail_ != head_) {
        buf[cnt++] = rx_buf_[tail_];
        tail_ = (tail_ + 1) % kMaxBufSize;
    }
    return cnt;
}

bool Uart::Send(const uint8_t* data, uint32_t len) const
{
    for (uint32_t i = 0; i < len; i++) {
        uart_poll_out(dev_, data[i]);
    }
    return true;
}

#endif // CONFIG_COM_UART

#ifdef CONFIG_COM_UART_DMA

void uart_dma_callback(const struct device* dev, struct uart_event* evt, void* user_data)
{
    auto* self = static_cast<UartDma*>(user_data);

    switch (evt->type)
    {
        case UART_RX_RDY:
        {
            const uint8_t* data = evt->data.rx.buf + evt->data.rx.offset;
            uint16_t len = evt->data.rx.len;
            if (len > 0)
            {
                if (self->rx_cb_) {
                    self->rx_cb_(const_cast<uint8_t*>(data), len);
                } else {
                    for (uint16_t i = 0; i < len; i++)
                    {
                        uint16_t next = (self->head_ + 1) % sizeof(self->rx_buf_);
                        if (next != self->tail_) {
                            self->rx_buf_[self->head_] = data[i];
                            self->head_ = next;
                        }
                    }
                    if (self->notify_sem_) {
                        k_sem_give(self->notify_sem_);
                    }
                }
            }
            break;
        }
        case UART_RX_BUF_REQUEST:
        {
            self->cur_buf_ = 1 - self->cur_buf_;
            uart_rx_buf_rsp(dev, self->dma_buf_[self->cur_buf_], sizeof(self->dma_buf_[0]));
            break;
        }
        case UART_RX_DISABLED:
        {
            self->cur_buf_ = 1 - self->cur_buf_;
            uart_rx_enable(dev, self->dma_buf_[self->cur_buf_], sizeof(self->dma_buf_[0]), self->rx_timeout_);
            break;
        }
        case UART_TX_DONE:
        {
            self->tx_busy_ = false;
            break;
        }
        default:
            break;
    }
}

bool UartDma::Init(const struct device* dev, const Config& cfg)
{
    dev_        = dev;
    head_       = 0;
    tail_       = 0;
    cur_buf_    = 0;
    rx_cb_      = nullptr;
    rx_timeout_ = cfg.rx_timeout;
    ready_      = false;
    tx_busy_    = false;

    if (!device_is_ready(dev_)) {
        return false;
    }

    int ret = uart_callback_set(dev_, uart_dma_callback, this);
    if (ret < 0) return false;

    uint16_t bs = cfg.buf_size > kMaxBufSize ? kMaxBufSize : cfg.buf_size;
    ret = uart_rx_enable(dev_, dma_buf_[0], bs, rx_timeout_);
    if (ret < 0) return false;

    ready_ = true;
    return true;
}

void UartDma::SetNotify(struct k_sem* sem)
{
    notify_sem_ = sem;
}

uint16_t UartDma::Read(uint8_t* buf, uint16_t max_len)
{
    uint16_t cnt = 0;
    while (cnt < max_len && tail_ != head_) {
        buf[cnt++] = rx_buf_[tail_];
        tail_ = (tail_ + 1) % sizeof(rx_buf_);
    }
    return cnt;
}

bool UartDma::Send(const uint8_t* data, uint32_t len)
{
    if (!ready_) return false;
    if (len == 0 || len >= (int)sizeof(tx_buf_)) return false;

    if (tx_busy_) return false;   // 上一帧还没发完

    memcpy(tx_buf_, data, len);
    tx_busy_ = true;

    if (uart_tx(dev_, reinterpret_cast<uint8_t*>(tx_buf_), len, 0) != 0) {
        tx_busy_ = false;
        return false;
    }
    return true;
}

void UartDma::Stop()
{
    ready_ = false;
    uart_rx_disable(dev_);
}

#endif // CONFIG_COM_UART_DMA
