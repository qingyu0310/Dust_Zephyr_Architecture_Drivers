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
#include <zephyr/sys/printk.h>

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

    bool Init(const struct device *dev, const struct can_filter &filter, can_mode_t ctrl_mode = CAN_MODE_NORMAL)
    {
        dev_ = dev;
        if (!device_is_ready(dev_)) {
            printk("Error: CAN device %s is not ready\n", dev_->name);
            return false;
        }

        filter_id_ = can_add_rx_filter(dev_, rx_callback, this, &filter);
        if (filter_id_ < 0) {
            printk("Error: CAN add RX filter failed (%d)\n", filter_id_);
            return false;
        }

        if (ctrl_mode != CAN_MODE_NORMAL) {
            (void)can_set_mode(dev_, ctrl_mode);
        }

        if (can_start(dev_) != 0) {
            printk("Error: CAN start failed\n");
            return false;
        }

        return true;
    }

    void SetTxCallback(TxCallback cb, void *user_data = nullptr)
    {
        tx_cb_      = cb;
        tx_cb_data_ = user_data;
    }

    void SetRxCallback(RxCallback cb, void *user_data = nullptr)
    {
        rx_cb_      = cb;
        rx_cb_data_ = user_data;
    }

    bool Send(const struct can_frame *frame)
    {
        if (dev_ == nullptr) return false;
        return can_send(dev_, frame, K_NO_WAIT, tx_callback, this) == 0;
    }

    const struct device *Device() const { return dev_; }

private:
    static void tx_callback(const struct device *dev, int error, void *user_data)
    {
        auto *self = static_cast<Can *>(user_data);
        if (self->tx_cb_ != nullptr) {
            self->tx_cb_(dev, error, self->tx_cb_data_);
        }
    }

    static void rx_callback(const struct device *dev, struct can_frame *frame,
                            void *user_data)
    {
        (void)dev;
        auto *self = static_cast<Can *>(user_data);
        if (self->rx_cb_ != nullptr) {
            self->rx_cb_(*frame, self->rx_cb_data_);
        }
    }

    const struct device *dev_{};
    int filter_id_ = -1;

    TxCallback tx_cb_      = nullptr;
    void*      tx_cb_data_ = nullptr;

    RxCallback rx_cb_      = nullptr;
    void*      rx_cb_data_ = nullptr;
};
