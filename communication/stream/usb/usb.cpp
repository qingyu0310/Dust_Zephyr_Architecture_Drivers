/**
 * @file usb.cpp
 * @author qingyu
 * @brief USB CDC ACM 顶层封装实现
 * @version 0.4
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "usb.hpp"
#include "log.hpp"


namespace usb {

/**
 * @brief 初始化
 *
 * 初始化顺序：device_.Init （含 CDC ACM Init） → Start
 */
bool Usb::Init(const UsbHal::Config& cfg, const UsbCdcAcmConfig& cdc_cfg)
{
    if (ready_) {
        return true;
    }

    // 注册数据回调（接收端点完成事件）
    device_.SetDataCallback(OnDataEvent, this);
    // 注册配置状态回调
    device_.SetConfigureCallback(OnConfigureEvent, this);

    if (!device_.Init(GetDefaultHal(), cfg, cdc_cfg)) {
        DUST_LOG_ERR("UsbDevPort init failed");
        return false;
    }

    // 启动 USB
    if (!device_.Start()) {
        DUST_LOG_ERR("UsbDevPort start failed");
        return false;
    }

    DUST_LOG_INF("USB initialized");
    ready_ = true;
    return true;
}

/**
 * @brief 从 UsbCdcAcm 转发的静态回调
 */
void Usb::OnDataEvent(void* ctx, uint8_t ep, const uint8_t* data, uint16_t len)
{
    if (ctx != nullptr) 
    {
        Usb* self = static_cast<Usb*>(ctx);

        if (ep == self->GetBulkOutEp()) {
            if (self->rx_queue_.Push(data, len)) {
                k_sem_give(&self->sem_);        // 唤醒等待线程
            }
        } else if (ep == self->GetBulkInEp()) {
            self->OnBulkIn(len);
        }
    }
}

/**
 * @brief 配置状态变化
 */
void Usb::OnConfigured(bool configured, uint16_t bulk_mps)
{
    k_spinlock_key_t key = k_spin_lock(&lock_);
    if (configured) {
        atomic_set(&configured_, 1);
        bulk_mps_   = bulk_mps;
    } 
    else {
        atomic_set(&configured_, 0);
        bulk_mps_   = 64;
        atomic_set(&tx_busy_, 0);
        // 取消配置时清空队列和信号量
        rx_queue_.Reset();
    }
    k_spin_unlock(&lock_, key);
}

/**
 * @brief 批量 IN 完成（设备→主机）
 *
 * 如果长度是 MPS 整数倍，发 ZLP 收尾。
 */
void Usb::OnBulkIn(uint16_t len)
{
    k_spinlock_key_t key = k_spin_lock(&lock_);
    if (!atomic_get(&configured_)) {
        atomic_set(&tx_busy_, 0);
        k_spin_unlock(&lock_, key);
        return;
    }

    if (len != 0 && (len % bulk_mps_) == 0) {
        if (!device_.GetHal().EpStartTx(GetBulkInEp(), nullptr, 0)) {
            atomic_set(&tx_busy_, 0);
        }
        k_spin_unlock(&lock_, key);
        return;
    }

    atomic_set(&tx_busy_, 0);
    k_spin_unlock(&lock_, key);
}

// Stream 接口

/**
 * @brief 发送一帧数据
 *
 * 仅在 configured 状态允许发送。
 * 发送失败时回滚 tx_busy_，避免永久锁死。
 */
bool Usb::Send(const uint8_t* data, uint32_t len)
{
    if (data == nullptr || len == 0 || len > kMaxBufSize) {
        return false;
    }

    // 临界区：检查配置 + 抢占发送 + 提交 DMA 必须连续完成
    // 防止 reset/disconnect 在中间关闭端点
    k_spinlock_key_t key = k_spin_lock(&lock_);
    if (!atomic_get(&configured_)) {
        k_spin_unlock(&lock_, key);
        return false;
    }
    if (!device_.GetCdcAcm().CanSend()) {
        k_spin_unlock(&lock_, key);
        return false;
    }
    if (atomic_cas(&tx_busy_, 0, 1) == 0) {
        k_spin_unlock(&lock_, key);
        return false;
    }

    bool ok = device_.GetHal().EpStartTx(GetBulkInEp(), data, static_cast<uint16_t>(len));
    if (!ok) {
        atomic_set(&tx_busy_, 0);
    }
    k_spin_unlock(&lock_, key);
    return ok;
}

} // namespace usb
