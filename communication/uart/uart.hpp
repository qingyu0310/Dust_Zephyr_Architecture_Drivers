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

/**
 * @brief 通用接收流接口
 *
 * 所有串行接收设备（UART、RS485、USB CDC 等）通过此接口统一对外暴露。
 */
class RxStream 
{
public:
    /**
     * @brief 接收缓冲配置
     */
    struct Config {
        uint16_t buf_size   = 128;  // 环形缓冲大小
        int32_t  rx_timeout = 0;    // 接收超时（ms，0=不限）
    };

    virtual ~RxStream() = default;
    virtual bool     Init(const struct device* dev, const Config& cfg) = 0;
    virtual void     SetNotify(struct k_sem* sem) = 0;
    virtual uint16_t Read(uint8_t* buf, uint16_t max_len) = 0;
};

/**
 * @brief UART 中断模式驱动
 *
 * FIFO 中断逐字节接收，存入环形缓冲。适用于低数据率场景。
 */
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

    const struct device* dev_ = nullptr;  // UART 设备
    uint16_t buf_size_ = 128;             // 环形缓冲大小
    uint8_t  rx_buf_[kMaxBufSize] {};     // 接收环形缓冲
    uint16_t head_     = 0;               // 环形缓冲写指针
    uint16_t tail_     = 0;               // 环形缓冲读指针
    k_sem*   notify_sem_ = nullptr;       // 接收通知信号量
};

/**
 * @brief UART DMA 模式驱动
 *
 * 双缓冲 DMA 接收 + 中断回调，支持高速连续接收。
 */
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

    const struct device* dev_ = nullptr;  // UART 设备

    // DMA 双缓冲
    uint8_t  dma_buf_[2][kMaxBufSize] {};
    uint8_t  rx_buf_[kMaxBufSize * 2] {};   // 接收合并缓冲
    uint16_t dma_buf_size_ = 0;             // DMA 单缓冲大小
    uint16_t head_        = 0;              // 环形缓冲写指针
    uint16_t tail_        = 0;              // 环形缓冲读指针
    uint8_t  cur_buf_     = 0;              // 当前使用的 DMA 缓冲索引
    int32_t  rx_timeout_  = 0;              // 接收超时（ms）
    bool     ready_       = false;          // 初始化完成标志

    UartRxCallback rx_cb_ = nullptr;        // 外部接收回调（替代环形缓冲）
    k_sem*   notify_sem_  = nullptr;        // 接收通知信号量

    // TX 单缓冲
    char     tx_buf_[128];   // 发送缓冲区
    bool     tx_busy_ = false;
};
