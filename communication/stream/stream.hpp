/**
 * @file stream.hpp
 * @author qingyu
 * @brief 串行流接口 — 统一收发抽象
 * @version 0.1
 * @date 2026-07-23
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

/**
 * @brief 串行流抽象接口
 *
 * 所有串行通信设备（UART、RS485、USB CDC 等）通过此接口统一对外暴露。
 */
class Stream
{
public:
    using RxCallback = void (*)(uint8_t* data, uint16_t len);
    using TxCallback = void (*)();

    struct BaseConfig {
        RxCallback rx_cb        = nullptr;          // 接收回调
        TxCallback tx_cb        = nullptr;          // 发送完成回调
        uint16_t   rx_timeout   = 1000;             // 接收超时（ms，0=不限）
    };

    Stream() { k_sem_init(&sem_, 0, 1); }
    virtual ~Stream() = default;

    virtual uint16_t Read(uint8_t* buf, uint16_t max_len) = 0;
    virtual bool     Send(const uint8_t* data, uint32_t len) = 0;

    k_sem sem_ {};                   // 接收通知信号量
};
