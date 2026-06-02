/**
 * @file can.hpp
 * @author qingyu
 * @brief CAN 总线驱动 — 中断接收回调 + 阻塞发送
 * @version 0.2
 * @date 2026-05-10
 */

#pragma once

#include "zephyr/device.h"
#include <zephyr/drivers/can.h>
#ifdef CONFIG_COM_CAN_STBY
#include <zephyr/drivers/gpio.h>
#endif

enum CanIndex : uint8_t
{
    USER_CAN1 = 0,
    USER_CAN2,
    USER_CAN3,
    USER_CAN4,
    USER_CAN5,
};

class Can final
{
public:
    using TxCallback = void (*)(const struct device *dev, int error, void *user_data);
    using RxCallback = void (*)(struct can_frame &frame, void *user_data);

#ifdef CONFIG_COM_CAN_STBY
    static void InitStby(const struct gpio_dt_spec *stby);
#endif

    bool Init(const struct device *dev, const struct can_filter &filter, can_mode_t ctrl_mode = CAN_MODE_NORMAL);
    void SetTxCallback(TxCallback cb, void *user_data = nullptr);
    void SetRxCallback(RxCallback cb, void *user_data = nullptr);
    bool Send(const struct can_frame *frame);

    const struct device *Device() const { return dev_; }

private:
    static void tx_callback(const struct device *dev, int error, void *user_data);
    static void rx_callback(const struct device *dev, struct can_frame *frame,
                            void *user_data);

    const struct device *dev_{};
    int filter_id_ = -1;

    TxCallback tx_cb_      = nullptr;
    void*      tx_cb_data_ = nullptr;

    RxCallback rx_cb_      = nullptr;
    void*      rx_cb_data_ = nullptr;
};
