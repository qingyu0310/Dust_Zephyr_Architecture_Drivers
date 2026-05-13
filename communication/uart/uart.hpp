/**
 * @file uart.hpp
 * @author qingyu
 * @brief UART 驱动 — 中断 / DMA + 通用 RxStream 接口
 * @version 0.3
 * @date 2026-05-13
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <stdint.h>

using UartRxCallback = void (*)(uint8_t* data, uint16_t len);

/*  通用 UART 接收流接口                                                  */
class RxStream {
public:
    struct Config {
        uint16_t buf_size   = 128;
        int32_t  rx_timeout = 0;      // 仅 DMA 版有效
    };

    virtual ~RxStream() = default;
    virtual bool     Init(const struct device* dev, const Config& cfg) = 0;
    virtual void     SetNotify(struct k_sem* sem) = 0;
    virtual uint16_t Read(uint8_t* buf, uint16_t max_len) = 0;
};

/*  中断驱动 UART（环缓冲区）                                              */
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

/*  DMA UART                                                              */
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
    k_sem    tx_sem_;
    bool     ready_       = false;

    UartRxCallback rx_cb_ = nullptr;
    k_sem*   notify_sem_  = nullptr;
};
