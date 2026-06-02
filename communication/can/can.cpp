/**
 * @file can.cpp
 * @author qingyu
 * @brief CAN driver implementation
 * @version 0.2
 * @date 2026-06-01
 */

#include "can.hpp"

#ifdef CONFIG_COM_CAN_STBY
void Can::InitStby(const struct gpio_dt_spec *stby)
{
    if (device_is_ready(stby->port)) {
        gpio_pin_configure_dt(stby, GPIO_OUTPUT_LOW);
    }
}
#endif

bool Can::Init(const struct device *dev, const struct can_filter &filter, can_mode_t ctrl_mode)
{
    dev_ = dev;
    if (!device_is_ready(dev_)) {
        return false;
    }

    filter_id_ = can_add_rx_filter(dev_, rx_callback, this, &filter);
    if (filter_id_ < 0) {
        return false;
    }

    if (ctrl_mode != CAN_MODE_NORMAL) {
        (void)can_set_mode(dev_, ctrl_mode);
    }

    if (can_start(dev_) != 0) {
        return false;
    }

    return true;
}

void Can::SetTxCallback(TxCallback cb, void *user_data)
{
    tx_cb_      = cb;
    tx_cb_data_ = user_data;
}

void Can::SetRxCallback(RxCallback cb, void *user_data)
{
    rx_cb_      = cb;
    rx_cb_data_ = user_data;
}

bool Can::Send(const struct can_frame *frame)
{
    if (dev_ == nullptr) return false;
    int ret = can_send(dev_, frame, K_NO_WAIT, tx_callback, this);
    return ret == 0;
}

void Can::tx_callback(const struct device *dev, int error, void *user_data)
{
    auto *self = static_cast<Can*>(user_data);
    if (self->tx_cb_ != nullptr) {
        self->tx_cb_(dev, error, self->tx_cb_data_);
    }
}

void Can::rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    (void)dev;
    auto *self = static_cast<Can*>(user_data);
    if (self->rx_cb_ != nullptr) {
        self->rx_cb_(*frame, self->rx_cb_data_);
    }
}
