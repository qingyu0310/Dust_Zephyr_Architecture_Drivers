/**
 * @file usb_transfer.hpp
 * @author qingyu
 * @brief USB 传输结果类型 — 明确错误码替代 nullptr+0 模糊语义
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

/**
 * @brief 传输错误类型
 *
 * HAL 报告传输完成时，通过此枚举区分正常完成、错误和取消。
 */
enum class TransferError : uint8_t {
    None,           // 正常完成
    Stall,          // 端点 STALL
    Cancelled,      // 传输被取消（如新 SETUP 到达、端点关闭）
    Transaction,    // 事务错误（qTD xact_err）
    Buffer,         // 缓冲错误（qTD buffer_err）
    Controller,     // 控制器错误（USBSTS.UEI）
    Timeout,        // 超时（未实现——预留）
};

/**
 * @brief 传输结果
 *
 * 替代旧有的 Event{data, length, error} 三元组。
 * 语义固定：
 *   - 正常零长度完成: error=None, length=0
 *   - 硬件错误: error!=None, data=nullptr, length=0
 *   - 取消完成: error=Cancelled
 *   - 旧代传输: sequence 不匹配则丢弃
 */
struct TransferResult {
    uint8_t        endpoint = 0;      // 端点号（含方向）
    const uint8_t* data     = nullptr; // 数据指针（仅 error=None 有效）
    uint16_t       length   = 0;      // 数据长度
    TransferError  error    = TransferError::None;
    uint32_t       sequence = 0;      // 传输序列号（用于识别代次）
};
