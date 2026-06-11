/**
 * @file rs485.hpp
 * @author qingyu
 * @brief RS485 half-duplex driver based on UART async API.
 * @version 0.1
 * @date 2026-05-23
 */

#pragma once

#include "uart.hpp"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

/**
 * @brief RS485 半双工驱动
 *
 * 基于 UART DMA + GPIO 方向控制引脚实现半双工收发。
 * 发送前拉高方向引脚，完成后自动切回接收。
 */
class Rs485 final : public RxStream
{
    friend void rs485_uart_callback(const struct device* dev, struct uart_event* evt, void* user_data);

public:
    /**
     * @brief RS485 初始化参数
     */
    struct Config : public RxStream::Config 
    {
        const struct gpio_dt_spec* dir = nullptr;  // 方向控制 GPIO
        int tx_level = 1;        // 发送时 GPIO 电平
        int rx_level = 0;        // 接收时 GPIO 电平
        int32_t tx_timeout = 0;  // 发送超时（ms）
    };

    bool     Init(const struct device* dev, const RxStream::Config& cfg) override;
    bool     Init(const struct device* dev, const Config& cfg);
    void     SetNotify(struct k_sem* sem) override;
    uint16_t Read(uint8_t* buf, uint16_t max_len) override;
    bool     Send(const uint8_t* data, uint32_t len);
    void     Stop();

    bool IsReady() const { return ready_; }
    bool IsTxBusy() const { return tx_busy_; }

private:
    static constexpr uint16_t kMaxBufSize = 512;
    static constexpr uint16_t kTxBufSize  = 256;

    bool SetDirection(int level);
    void StoreRx(const uint8_t* data, uint16_t len);

    const struct device* dev_       = nullptr;  // UART 设备
    const struct gpio_dt_spec* dir_ = nullptr;  // 方向 GPIO

    // DMA 双缓冲
    uint8_t  dma_buf_[2][kMaxBufSize] {};
    uint8_t  rx_buf_[kMaxBufSize * 2] {};    // 接收合并缓冲
    uint8_t  tx_buf_[kTxBufSize] {};         // 发送缓冲
    uint16_t dma_buf_size_ = 0;              // DMA 单缓冲大小
    uint16_t head_         = 0;              // 环形缓冲写指针
    uint16_t tail_         = 0;              // 环形缓冲读指针
    uint8_t  cur_buf_      = 0;              // 当前 DMA 缓冲索引
    int32_t  rx_timeout_   = 0;              // 接收超时（ms）
    int32_t  tx_timeout_   = 0;              // 发送超时（ms）
    int      tx_level_     = 1;              // 发送方向 GPIO 电平
    int      rx_level_     = 0;              // 接收方向 GPIO 电平
    bool     ready_        = false;
    bool     tx_busy_      = false;

    k_sem* notify_sem_ = nullptr;
};
