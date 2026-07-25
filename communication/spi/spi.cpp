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
