/**
 * @file can.hpp
 * @author qingyu
 * @brief CAN 总线驱动，支持发送和中断接收
 * @version 0.1
 * @date 2026-04-28
 *
 * @copyright Copyright (c) 2026
 *
 * @par 使用示例
 *
 *      // 声明全局对象
 *      Can g_can;
 *
 *      // 初始化（接收所有标准帧，带 STBY 使能）
 *      const struct gpio_dt_spec stby = { .port = DEVICE_DT_GET(DT_NODELABEL(gpioz)), .pin = 2, .dt_flags = 0 };
 *      const struct can_filter filter = { .id = 0x200, .mask = CAN_STD_ID_MASK, .flags = 0 };
 *      g_can.Init(DEVICE_DT_GET(DT_NODELABEL(mcan4)), filter, CAN_MODE_NORMAL, &stby);
 *
 *      // 注册 TX 完成回调（可选，不注册不回调）
 *      g_can.SetTxCallback([](const struct device *, int error, void *) {
 *          if (error) printk("TX err %d\n", error);
 *      });
 *
 *      // 注册 RX 中断回调（可选，不注册进 ring buffer 仍可用 Read()）
 *      g_can.SetRxCallback([](struct can_frame &frame, void *) {
 *          printk("RX id=0x%x\n", frame.id);
 *      });
 *
 *      // 发送
 *      struct can_frame tx{ .id = 0x200, .dlc = 8, .data = {0} };
 *      g_can.Send(&tx);
 *
 *      // 轮询接收（中断自动填充 ring buffer）
 *      struct can_frame rx;
 *      if (g_can.Read(&rx)) {
 *          // 处理 rx
 *      }
 * @endcode
 */

#pragma once

#include "zephyr/device.h"
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

enum CanIndex : uint8_t
{
    CAN1 = 0,
    CAN2,
    CAN3,
    CAN4,
    CAN5,
};

class Can final
{
public:
    /** 发送完成回调函数类型 */
    using TxCallback = void (*)(const struct device *dev, int error, void *user_data);
    /** 接收回调函数类型: void callback(struct can_frame &frame, void *user_data) */
    using RxCallback = void (*)(struct can_frame &frame, void *user_data);

    /**
     * @brief 初始化 CAN 控制器（默认不过滤）
     * @param dev  CAN 设备指针
     * @param stby 收发器 STBY gpio_dt_spec（可选，nullptr 不配置）
     */
    bool Init(const struct device *dev,
              const struct gpio_dt_spec *stby = nullptr)
    {
        const struct can_filter all_pass = { .id = 0, .mask = 0, .flags = 0 };
        return Init(dev, all_pass, CAN_MODE_NORMAL, stby);
    }

    /**
     * @brief 初始化 CAN 控制器
     * @param dev    CAN 设备指针
     * @param filter 接收过滤器（可传临时量）
     *
     * @par 过滤器配置说明
     *      filter 决定哪些 ID 的帧会被接收:
     *      - id   匹配的 CAN ID（对齐到高位）
     *      - mask 掩码，0 表示忽略对应位，1 表示精确匹配
     *      - flags 设 CAN_FILTER_IDE 匹配扩展帧
     *
     *      | 场景                         | id              | mask             | 效果              |
     *      |------------------------------|-----------------|------------------|-------------------|
     *      | 收所有标准帧                  | 0               | 0                | 不过滤            |
     *      | 只收 ID=0x200                | 0x200           | CAN_STD_ID_MASK  | 精确匹配 11-bit   |
     *      | 收一组标准帧 ID=0x200~0x203  | 0x200           | 0x7F8            | 忽略最低 3 位     |
     *      | 只收扩展帧 ID=0x12345678     | 0x12345678      | CAN_EXT_ID_MASK  | 精确匹配 29-bit   |
     */
    bool Init(const struct device *dev, const struct can_filter &filter,
              can_mode_t ctrl_mode = CAN_MODE_NORMAL,
              const struct gpio_dt_spec *stby = nullptr)
    {
        dev_ = dev;
        if (!device_is_ready(dev_)) {
            printk("Error: CAN device %s is not ready\n", dev_->name);
            return false;
        }

        /* Enable transceiver if STBY gpio provided */
        if (stby != nullptr && device_is_ready(stby->port)) {
            gpio_pin_configure_dt(stby, GPIO_OUTPUT_LOW);
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

    /**
     * @brief 注册发送完成回调
     * @param cb        回调函数指针，传入 nullptr 取消
     * @param user_data 透传给回调的用户参数
     */
    void SetTxCallback(TxCallback cb, void *user_data = nullptr)
    {
        tx_cb_      = cb;
        tx_cb_data_ = user_data;
    }

    /**
     * @brief 注册接收回调（中断中直接触发）
     * @param cb        回调函数指针，传入 nullptr 取消
     * @param user_data 透传给回调的用户参数
     *
     * @note 注册后收到帧时会先调回调，再存入环缓冲区（仍可通过 Read() 轮询）
     */
    void SetRxCallback(RxCallback cb, void *user_data = nullptr)
    {
        rx_cb_      = cb;
        rx_cb_data_ = user_data;
    }

    /**
     * @brief 发送一帧 CAN 数据
     * @param frame 待发送帧
     * @return true 发送成功
     */
    bool Send(const struct can_frame *frame)
    {
        if (dev_ == nullptr) return false;
        return can_send(dev_, frame, K_NO_WAIT, tx_callback, this) == 0;
    }

    /**
     * @brief 获取 CAN 设备指针
     */
    const struct device *Device() const { return dev_; }

    /**
     * @brief 从接收环缓冲区读取一帧
     * @param frame 输出参数，收到的帧
     * @return true  读到一帧
     * @return false 缓冲区空
     */
    bool Read(struct can_frame *frame)
    {
        uint32_t tail = rx_tail_;
        if (tail == rx_head_) {
            return false;
        }
        *frame = rx_buf_[tail];
        rx_tail_ = (tail + 1) % RX_BUF_SIZE;
        return true;
    }

private:
    static constexpr uint32_t RX_BUF_SIZE = 128;

    /** CAN TX 完成回调（分派到用户注册的 tx_cb_） */
    static void tx_callback(const struct device *dev, int error, void *user_data)
    {
        auto *self = static_cast<Can *>(user_data);
        if (self->tx_cb_ != nullptr) {
            self->tx_cb_(dev, error, self->tx_cb_data_);
        }
    }

    /**
     * @brief CAN 接收中断回调（静态，由 Zephyr 驱动调用）
     */
    static void rx_callback(const struct device *dev, struct can_frame *frame,
                            void *user_data)
    {
        (void)dev;
        auto *self = static_cast<Can *>(user_data);

        /* 先触发用户回调 */
        if (self->rx_cb_ != nullptr) {
            self->rx_cb_(*frame, self->rx_cb_data_);
        }

        /* 再存入环缓冲区 */
        uint32_t head = self->rx_head_;
        uint32_t next = (head + 1) % RX_BUF_SIZE;
        if (next != self->rx_tail_) {
            self->rx_buf_[head] = *frame;
            self->rx_head_ = next;
        }
    }

    const struct device *dev_{};
    int filter_id_ = -1;

    struct can_frame rx_buf_[RX_BUF_SIZE]{};
    volatile uint32_t rx_head_ = 0;
    volatile uint32_t rx_tail_ = 0;

    TxCallback tx_cb_      = nullptr;
    void* tx_cb_data_ = nullptr;

    RxCallback rx_cb_      = nullptr;
    void* rx_cb_data_ = nullptr;
};
