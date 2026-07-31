/**
 * @file spi.cpp
 * @author qingyu
 * @brief SPI driver implementation
 * @version 0.1
 * @date 2026-06-01
 */

#include "spi.hpp"
#include <zephyr/logging/log.h>

#pragma message "Compiling Drivers/Communication SPI"

LOG_MODULE_REGISTER(spi, LOG_LEVEL_INF);

/**
 * @brief 通过设备树初始化 SPI
 * @param spec  SPI 设备树描述（总线 + 配置）
 */
bool Spi::Init(const struct spi_dt_spec& spec)
{
    if (!spi_is_ready_dt(&spec)) {
        LOG_ERR("spi not ready");
        return false;
    }

    spec_  = spec;
    ready_ = true;
    tx_set_.buffers = &tx_buf_;
    tx_set_.count   = 1;
    rx_set_.buffers = &rx_buf_;
    rx_set_.count   = 1;

    LOG_INF("spi ready");

    return true;
}

/**
 * @brief 全双工收发
 */
bool Spi::Transceive(const uint8_t* tx_data, uint8_t* rx_data, uint32_t len)
{
    if (!ready_ || !PrepareTx(tx_data, len) || !PrepareRx(rx_data, len)) {
        return false;
    }

    return spi_transceive_dt(&spec_, &tx_set_, &rx_set_) == 0;
}

/**
 * @brief 半双工写
 */
bool Spi::Send(const uint8_t* data, uint32_t len)
{
    if (!ready_ || !PrepareTx(data, len)) {
        return false;
    }

    return spi_write_dt(&spec_, &tx_set_) == 0;
}

/**
 * @brief 半双工读
 */
bool Spi::Read(uint8_t* data, uint32_t len)
{
    if (!ready_ || !PrepareRx(data, len)) {
        return false;
    }

    return spi_read_dt(&spec_, &rx_set_) == 0;
}

/**
 * @brief 先写后读（CS 保持有效，单事务）
 *
 * 用 SPI_HOLD_ON_CS 保证 write+read 之间 CS 不断，
 * 避免半双工 split 导致 CS 释放。
 */
bool Spi::WriteThenRead(const uint8_t* tx_data, uint32_t tx_len,
                        uint8_t* rx_data, uint32_t rx_len)
{
    if (!ready_ || tx_data == nullptr || tx_len == 0 || rx_data == nullptr || rx_len == 0) {
        return false;
    }

    const uint8_t op_saved = spec_.config.operation;

    // 写阶段：HOLD_ON_CS → CS 保持选通
    spec_.config.operation = op_saved | SPI_HOLD_ON_CS;
    if (!PrepareTx(tx_data, tx_len) || spi_write_dt(&spec_, &tx_set_) != 0) {
        spec_.config.operation = op_saved;
        return false;
    }

    // 读阶段：释放 HOLD_ON_CS → CS 在读完后释放
    spec_.config.operation = op_saved & ~SPI_HOLD_ON_CS;
    if (!PrepareRx(rx_data, rx_len) || spi_read_dt(&spec_, &rx_set_) != 0) {
        spec_.config.operation = op_saved;
        return false;
    }

    spec_.config.operation = op_saved;
    return true;
}

/**
 * @brief 准备发送缓冲描述符
 */
bool Spi::PrepareTx(const uint8_t* data, uint32_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    tx_buf_.buf = const_cast<uint8_t*>(data);
    tx_buf_.len = len;
    return true;
}

/**
 * @brief 准备接收缓冲描述符
 */
bool Spi::PrepareRx(uint8_t* data, uint32_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    rx_buf_.buf = data;
    rx_buf_.len = len;
    return true;
}
