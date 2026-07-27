/**
 * @file usb_control.hpp
 * @author qingyu
 * @brief EP0 控制传输上下文 — 状态跟踪 + 序列号防旧 completion
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>
#include "usb_types.hpp"

/**
 * @brief EP0 控制传输阶段
 */
enum class Ep0Stage : uint8_t {
    Idle,                  // 空闲/等待 SETUP
    DataIn,                // DATA IN 阶段
    DataOut,               // DATA OUT 阶段
    StatusIn,              // STATUS IN 阶段
    StatusOut,             // STATUS OUT 阶段
};

/**
 * @brief EP0 控制传输上下文
 *
 * 新 SETUP 到达时 sequence 递增，旧 completion 通过 sequence 匹配丢弃。
 */
struct ControlTransfer {
    uint32_t          sequence  = 0;                // 传输序列号
    usb::SetupPacket  setup     {};                 // 当前 SETUP
    Ep0Stage          stage     = Ep0Stage::Idle;   // 当前 STAGE
    const uint8_t*    data_in   = nullptr;          // DATA IN 指针
    uint16_t          data_len  = 0;                // 数据总长
    uint16_t          data_sent = 0;                // 已发送量（分段使用）
};
