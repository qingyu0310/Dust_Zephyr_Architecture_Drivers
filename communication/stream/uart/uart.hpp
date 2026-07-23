/**
 * @file uart.hpp
 * @author qingyu
 * @brief UART DMA 驱动 - 双缓冲 DMA + Stream 接口
 * @version 0.5
 * @date 2026-07-23
 *
 * # UART DMA 使用说明
 *
 * 双缓冲 DMA 接收，适合高速连续接收。
 * ### 设备树
 * 项目 overlay 中定义 alias：
 * ```dts
 * aliases {
 *     uart-remote = &uart4;
 * };
 * ```
 *
 * ### Kconfig
 * ```kconfig
 * config TRD_REMOTE
 *     select COM_UART_DMA
 * ```
 *
 * ### 初始化
 * ```cpp
 * static UartDma uart;
 *
 * uart.Init(DEVICE_DT_GET(DT_ALIAS(uart_remote)));
 * ```
 *
 * ### 接收回调
 * ```cpp
 * uart.SetRxCallback([](uint8_t* data, uint16_t len) {
 *     // 处理接收数据
 * });
 * ```
 *
 * ### 发送
 * ```cpp
 * uart.Send(data, len);  // DMA 异步发送
 * ```
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <stdint.h>
#include "bipbuf.hpp"
#include "../stream.hpp"

#ifdef CONFIG_COM_UART_DMA

/**
 * @brief UART DMA 模式驱动
 *
 * 双缓冲 DMA 接收 + 中断回调，支持高速连续接收。 
 */
class UartDma final : public Stream
{
    friend void uart_dma_callback(const struct device* dev, struct uart_event* evt, void* user_data);

public:
    struct Config 
    {
        uart_config line_cfg {
            115200,
            UART_CFG_PARITY_NONE,
            UART_CFG_STOP_BITS_1,
            UART_CFG_DATA_BITS_8,
            UART_CFG_FLOW_CTRL_NONE,
        };
        BaseConfig  base_cfg {};
    };

    bool     Init(const struct device* dev, const Config& cfg);
    void     SetBaudrate(uint32_t baud);
    void     SetLineConfig(const uart_config& cfg);

    bool     StartRx();
    void     StopRx();

    uint16_t Read(uint8_t* buf, uint16_t max_len)    override;
    bool     Send(const uint8_t* data, uint32_t len) override;

private:
    static constexpr uint16_t kMaxBufSize = 256;    // 缓冲区最大值

    uint8_t  dma_buf_[2][kMaxBufSize] {};           // 硬件 DMA 双缓冲
    uint8_t  cur_buf_ = 0;                          // 当前提交给硬件的 DMA buffer 索引
    bool     buf_free_[2] = {true, true};   // DMA buffer 空闲状态跟踪

    BipBuffer<kMaxBufSize * 2> rx_bip_ {};          // 软件缓冲队列

    uint8_t  tx_data_[128] {};                      // 发送缓冲
    uint8_t* tx_buf_ = tx_data_;                    // 指向发送缓冲
    atomic_t tx_busy_ = ATOMIC_INIT(0);

    const device* dev_ = nullptr;

    Config      config_ {};
    atomic_t    ready_ = ATOMIC_INIT(0);            // 初始化完成标志
    k_spinlock  lock_;
    
    bool ApplyLineConfig();

    void Reset()
    {
        cur_buf_ = 0;
        buf_free_[0] = true;
        buf_free_[1] = true;
        atomic_set(&tx_busy_, 0);
        atomic_set(&ready_, 0);
    }
};

#endif // CONFIG_COM_UART_DMA


