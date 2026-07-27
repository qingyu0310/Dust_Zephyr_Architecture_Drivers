/**
 * @file usb.cpp
 * @author qingyu
 * @brief USB CDC ACM 顶层封装实现
 * @version 0.4
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#include "usb.hpp"

#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb, LOG_LEVEL_INF);

static constexpr uint8_t kCdcOutEp = 0x01;  // 批量 OUT 端点
static constexpr uint8_t kCdcInEp  = 0x81;  // 批量 IN 端点

/**
 * @brief 初始化
 *
 * 初始化顺序：cdc_ → device_
 */
bool Usb::Init(const Config& cfg)
{
    if (ready_) {
        return true;
    }

    if (cfg.hal == nullptr) {
        return false;
    }

    hal_ = cfg.hal;

    // 初始化 CDC ACM（描述符集在内部创建）
    UsbCdcAcm::Config cdc_cfg {};
    if (!cdc_.Init(device_, cdc_cfg)) {
        LOG_ERR("CDC ACM init failed");
        return false;
    }

    // 注册数据回调（接收端点完成事件）
    cdc_.SetDataCallback(OnDataEvent, this);
    // 注册配置状态回调
    cdc_.SetConfigureCallback(OnConfigureEvent, this);

    // 初始化 USB 设备核心
    UsbHal::Config hal_cfg {};
    hal_cfg.busid         = cfg.busid;
    hal_cfg.reg_base      = cfg.reg_base;
    hal_cfg.irq_num       = cfg.irq_num;
    hal_cfg.irq_priority  = cfg.irq_priority;

    if (!device_.Init(*hal_, cdc_, hal_cfg)) {
        LOG_ERR("UsbDevice init failed");
        return false;
    }

    // 启动 USB
    if (!device_.Start()) {
        LOG_ERR("UsbDevice start failed");
        return false;
    }

    LOG_INF("USB initialized");
    ready_ = true;
    return true;
}

// 数据事件回调

/**
 * @brief 从 UsbCdcAcm 转发的静态回调
 */
void Usb::OnDataEvent(void* ctx, uint8_t ep, const uint8_t* data, uint16_t len)
{
    if (ctx != nullptr) {
        Usb* self = static_cast<Usb*>(ctx);

        if (ep == kCdcOutEp) {
            self->OnBulkOut(data, len);
        } else if (ep == kCdcInEp) {
            self->OnBulkIn(len);
        }
    }
}

/**
 * @brief 从 UsbCdcAcm 转发的配置状态回调
 */
void Usb::OnConfigureEvent(void* ctx, bool configured, uint16_t bulk_mps)
{
    if (ctx != nullptr) {
        static_cast<Usb*>(ctx)->OnConfigured(configured, bulk_mps);
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
    } else {
        atomic_set(&configured_, 0);
        bulk_mps_   = 64;
        atomic_set(&tx_busy_, 0);
        // 取消配置时清空信号量，防止旧通知残留
        k_sem_reset(&sem_);
    }
    k_spin_unlock(&lock_, key);
}

/**
 * @brief 批量 OUT 完成（主机→设备）
 *
 * data 指向 HAL 内部 DMA 缓冲，回调返回后 HAL 可能重用此缓冲。
 * 必须在返回前将数据拷贝到 rx_bip_。
 */
void Usb::OnBulkOut(const uint8_t* data, uint16_t len)
{
    // 写入 BipBuffer（临界区：ISR vs Read 线程）
    k_spinlock_key_t key = k_spin_lock(&lock_);
    uint8_t* p = rx_bip_.Reserve(len);
    if (p != nullptr) {
        memcpy(p, data, len);
        rx_bip_.Commit(len);
        k_spin_unlock(&lock_, key);
        k_sem_give(&sem_);
    } else {
        k_spin_unlock(&lock_, key);
    }
}

/**
 * @brief 批量 IN 完成（设备→主机）
 *
 * 如果长度是 MPS 整数倍，发 ZLP 收尾。
 */
void Usb::OnBulkIn(uint16_t len)
{
    if (len != 0 && (len % bulk_mps_) == 0) {
        // ZLP 也在临界区内提交，防止 teardown 在中间执行
        k_spinlock_key_t key = k_spin_lock(&lock_);
        if (!atomic_get(&configured_)) {
            atomic_set(&tx_busy_, 0);
            k_spin_unlock(&lock_, key);
            return;
        }
        if (!hal_->EpStartTx(kCdcInEp, nullptr, 0)) {
            atomic_set(&tx_busy_, 0);
        }
        k_spin_unlock(&lock_, key);
        return;
    }

    atomic_set(&tx_busy_, 0);
}

// Stream 接口

/**
 * @brief 读取接收数据
 *
 * 从 BipBuffer 取出连续数据块。
 */
uint16_t Usb::Read(uint8_t* buf, uint16_t max_len)
{
    if (buf == nullptr || max_len == 0) {
        return 0;
    }

    k_spinlock_key_t key = k_spin_lock(&lock_);
    uint32_t avail = 0;
    uint8_t* p = rx_bip_.GetContiguousReadBlock(avail);
    if (p == nullptr || avail == 0) {
        k_spin_unlock(&lock_, key);
        return 0;
    }

    uint16_t cnt = (max_len < avail) ? max_len : static_cast<uint16_t>(avail);
    memcpy(buf, p, cnt);
    rx_bip_.Decommit(cnt);
    k_spin_unlock(&lock_, key);

    return cnt;
}

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
    if (atomic_cas(&tx_busy_, 0, 1) == 0) {
        k_spin_unlock(&lock_, key);
        return false;
    }

    bool ok = hal_->EpStartTx(kCdcInEp, data, static_cast<uint16_t>(len));
    if (!ok) {
        atomic_set(&tx_busy_, 0);
    }
    k_spin_unlock(&lock_, key);
    return ok;
}
