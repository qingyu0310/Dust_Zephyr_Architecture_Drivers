/**
 * @file usb_rx_queue.hpp
 * @author qingyu
 * @brief USB 接收队列 — 封装 BipBuffer + 锁 + 通知 semaphore
 * @version 0.1
 * @date 2026-07-27
 *
 * 职责：
 *   - Push（ISR 上下文）：写入数据，超出容量计数 overflow
 *   - Pop（线程上下文）：取出数据
 *   - Reset：清空数据和通知，重置 overflow 计数
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

#include "bipbuf.hpp"

/**
 * @brief USB 接收队列
 *
 * 内部使用 BipBuffer<1024>，通过 spinlock 保护 ISR/线程并发访问。
 * 内置 k_sem，Push 成功后自动 give，消费者可等待此 semaphore。
 */
class UsbRxQueue final
{
public:
    UsbRxQueue()
    {
        k_sem_init(&sem_, 0, 1);
    }

    /**
     * @brief 写入数据（ISR 或线程上下文）
     * @param data  数据指针
     * @param len   数据长度
     * @return true=写入成功，false=空间不足（overflow 计数+1）
     */
    bool Push(const uint8_t* data, uint16_t len)
    {
        k_spinlock_key_t key = k_spin_lock(&lock_);
        uint8_t* p = buf_.Reserve(len);
        if (p == nullptr) {
            overflow_++;
            k_spin_unlock(&lock_, key);
            return false;
        }
        memcpy(p, data, len);
        buf_.Commit(len);
        k_spin_unlock(&lock_, key);

        k_sem_give(&sem_);
        return true;
    }

    /**
     * @brief 取出数据（线程上下文）
     * @param buf     输出缓冲区
     * @param max_len 最大长度
     * @return 实际读取字节数，0=无数据
     */
    uint16_t Pop(uint8_t* buf, uint16_t max_len)
    {
        if (buf == nullptr || max_len == 0) {
            return 0;
        }

        k_spinlock_key_t key = k_spin_lock(&lock_);
        uint32_t avail = 0;
        uint8_t* p = buf_.GetContiguousReadBlock(avail);
        if (p == nullptr || avail == 0) {
            k_spin_unlock(&lock_, key);
            return 0;
        }

        uint16_t cnt = (max_len < avail) ? max_len : static_cast<uint16_t>(avail);
        memcpy(buf, p, cnt);
        buf_.Decommit(cnt);
        k_spin_unlock(&lock_, key);
        return cnt;
    }

    /**
     * @brief 复位 — 清 overflow + 重置 semaphore
     */
    void Reset()
    {
        overflow_ = 0;
        k_sem_reset(&sem_);
    }

    /**
     * @brief 获取 overflow 计数
     */
    uint32_t OverflowCount() const
    {
        return overflow_;
    }

    /**
     * @brief 获取通知 semaphore（供外部等待数据用）
     */
    struct k_sem* GetSem()
    {
        return &sem_;
    }

private:
    BipBuffer<1024> buf_ {};
    k_spinlock      lock_ {};
    k_sem           sem_ {};
    uint32_t        overflow_ = 0;
};
