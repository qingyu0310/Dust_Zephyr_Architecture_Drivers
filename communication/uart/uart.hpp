/**
 * @file uart.hpp
 * @author qingyu
 * @brief UART 驱动 — 中断 / DMA + 通用 RxStream 接口
 * @version 0.4
 * @date 2026-05-16
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <stdint.h>

using UartRxCallback = void (*)(uint8_t* data, uint16_t len);

class RxStream {
public:
    struct Config {
        uint16_t buf_size   = 128;
        int32_t  rx_timeout = 0;
    };

    virtual ~RxStream() = default;
    virtual bool     Init(const struct device* dev, const Config& cfg) = 0;
    virtual void     SetNotify(struct k_sem* sem) = 0;
    virtual uint16_t Read(uint8_t* buf, uint16_t max_len) = 0;
};

class Uart final : public RxStream
{
    friend void uart_irq_handler(const struct device* dev, void* user_data);

public:
    bool     Init(const struct device* dev, const Config& cfg) override;
    void     SetNotify(struct k_sem* sem) override;
    uint16_t Read(uint8_t* buf, uint16_t max_len) override;
    bool     Send(const uint8_t* data, uint32_t len) const;

private:
    static constexpr uint16_t kMaxBufSize = 512;
    const struct device* dev_ = nullptr;
    uint16_t buf_size_ = 128;
    uint8_t  rx_buf_[kMaxBufSize] {};
    uint16_t head_     = 0;
    uint16_t tail_     = 0;
    k_sem*   notify_sem_ = nullptr;
};

class UartDma final : public RxStream
{
    friend void uart_dma_callback(const struct device* dev, struct uart_event* evt, void* user_data);

public:
    bool     Init(const struct device* dev, const Config& cfg) override;
    void     SetNotify(struct k_sem* sem) override;
    uint16_t Read(uint8_t* buf, uint16_t max_len) override;
    bool     Send(const uint8_t* data, uint32_t len);
    void     Stop();

private:
    static constexpr uint16_t kMaxBufSize = 512;

    const struct device* dev_ = nullptr;

    uint8_t  dma_buf_[2][kMaxBufSize] {};
    uint8_t  rx_buf_[kMaxBufSize * 2] {};
    uint16_t head_        = 0;
    uint16_t tail_        = 0;
    uint8_t  cur_buf_     = 0;
    int32_t  rx_timeout_  = 0;
    bool     ready_       = false;

    UartRxCallback rx_cb_ = nullptr;
    k_sem*   notify_sem_  = nullptr;

    // TX 单缓冲
    char     tx_buf_[128];   // DMA 发送缓冲
    bool     tx_busy_ = false;
};
