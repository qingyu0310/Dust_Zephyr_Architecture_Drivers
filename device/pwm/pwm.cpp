/**
 * @file pwm.cpp
 * @author qingyu
 * @brief PWM 输出驱动
 * @version 0.1
 * @date 2026-06-11
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "pwm.hpp"
#include "log.hpp"

#pragma message "Compiling Drivers/Device Pwm"


/**
 * @brief 初始化 PWM 输出
 * @param spec  设备树 PWM 规格
 */
bool Pwm::init(const pwm_dt_spec& spec)
{
    spec_ = spec;
    if (!device_is_ready(spec.dev)) {
        DUST_LOG_ERR("device not ready %s", spec.dev->name);
        return false;
    }

    if (!SetPulse(0)) {
        DUST_LOG_ERR("set pulse 0 fail");
        return false;
    }
    DUST_LOG_INF("pwm ready ch=%d", spec.channel);
    return true;
}

/**
 * @brief 设置脉宽（基于 spec_.period）
 * @param pulse 脉宽 ns
 */
bool Pwm::SetPulse(uint32_t pulse)
{
    int ret = pwm_set_pulse_dt(&spec_, pulse);
    if (ret != 0) {
        DUST_LOG_ERR("set pulse %u fail %d ch=%u", pulse, ret, spec_.channel);
        return false;
    }
    return true;
}

/**
 * @brief 按占空比设置脉宽
 * @param duty 占空比 0.0～1.0
 */
bool Pwm::SetDuty(float duty)
{
    if (duty < 0.0f) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    }

    const uint32_t pulse = static_cast<uint32_t>(spec_.period * duty);
    return SetPulse(pulse);
}

/**
 * @brief 同时设置周期和脉宽
 * @param period 周期 ns
 * @param pulse  脉宽 ns
 */
bool Pwm::SetPeriodAndPulse(uint32_t period, uint32_t pulse)
{
    int ret = pwm_set_dt(&spec_, period, pulse);
    if (ret != 0) {
        DUST_LOG_ERR("set period=%u pulse=%u fail %d ch=%u", period, pulse, ret, spec_.channel);
        return false;
    }

    spec_.period = period;
    return true;
}

/**
 * @brief 停止输出
 * 
 * @return true 
 * @return false 
 */
bool Pwm::Stop()
{
    return SetPulse(0);
}
