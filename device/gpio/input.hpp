/**
 * @file input.hpp
 * @author qingyu
 * @brief 
 * @version 0.1
 * @date 2026-04-23
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include "zephyr/device.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

class Input final
{
public:
    bool init(const gpio_dt_spec spec, gpio_flags_t extra_flags = GPIO_INPUT) 
    {
        spec_ = spec;
        if (!device_is_ready(spec.port)) {
            printk("Error: GPIO device %s is not ready\n", spec.port->name);
            return false;
        }
        int ret = gpio_pin_configure_dt(&spec, extra_flags);
        if (ret != 0) {
            printk("Error %d: failed to configure pin %d\n", ret, spec.pin);
            return false;
        }
        return true;
    }

    bool IsActivated() 
    {
        int val = gpio_pin_get_dt(&spec_);
        if (val < 0) {
            input_bit_ = 0;
            return false;
        }
        bool actived_bit = (val != 0);
        input_bit_ = (input_bit_ << 1) | actived_bit;
        return (input_bit_ & 0xFFFF) == 0xFFFF;
    }

private:
    gpio_dt_spec spec_{};
    
    uint16_t input_bit_ = 0x0000;
};