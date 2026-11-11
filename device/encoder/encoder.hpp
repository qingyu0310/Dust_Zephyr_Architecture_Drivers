/**
 * @file encoder.hpp
 * @author qingyu
 * @brief STM32 定时器编码器设备 — C++ 薄包装（转发到 encoder.h 的 inline 访问函数）
 * @version 1.1
 * @date 2026-11-12
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "encoder.h"

/**
 * @brief 编码器 C++ 包装（只持 const device*，不拥有设备）
 *
 * 所有方法转发到 encoder.h 的 inline 访问函数，不加任何逻辑。
 */
class Encoder
{
public:
    /**
     * @brief 绑定设备实例并检查就绪
     * @param dev 编码器设备（DEVICE_DT_GET(DT_NODELABEL(encoder_xxx))）
     * @return true=设备已就绪
     */
    bool Init(const device* dev)
    {
        if (dev == nullptr || !device_is_ready(dev)) {
            return false;
        }
        dev_ = dev;
        return true;
    }

    /**
     * @brief 周期采样：算增量 / 累计 / 测速 / 方向
     */
    void Update() { encoder_update(dev_); }

    /**
     * @brief 累计计数（可正可负）
     */
    int32_t GetCount() const { return encoder_get_count(dev_); }

    /**
     * @brief 计数/秒
     */
    float GetCountsPerSec() const { return encoder_get_counts_per_sec(dev_); }

    /**
     * @brief 圈/秒 = 计数/秒 ÷ (ppr × 4)
     */
    float GetSpeed() const { return encoder_get_speed(dev_); }

    /**
     * @brief 方向：1 = 正转，-1 = 反转，0 = 静止
     */
    int8_t GetDirection() const { return encoder_get_direction(dev_); }

    /**
     * @brief 计数与速度清零（不动硬件 CNT）
     */
    void Reset() { encoder_reset(dev_); }

    /**
     * @brief 每圈脉冲数（dts 的 st,counts-per-revolution）
     */
    uint16_t GetCountsPerRev() const { return encoder_get_counts_per_rev(dev_); }

private:
    const device* dev_ = nullptr;    // 编码器设备实例
};
