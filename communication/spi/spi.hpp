/**
 * @file spi.hpp
 * @author qingyu
 * @brief SPI driver - device-tree-based synchronous read/write and full-duplex transfer
 * @version 0.1
 * @date 2026-06-01
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

class Spi final
{
public:
    bool Init(const struct spi_dt_spec& spec);

    bool Transceive(const struct spi_buf_set* tx, const struct spi_buf_set* rx) const;
    bool Send(const struct spi_buf_set* tx) const;
    bool Read(const struct spi_buf_set* rx) const;

    bool Transceive(const uint8_t* tx_data, uint8_t* rx_data, uint32_t len);
    bool Send(const uint8_t* data, uint32_t len);
    bool Read(uint8_t* data, uint32_t len);
    
    bool Release() const;
    bool IsReady() const;

    const struct device* Device() const { return spec_.bus; }
    const struct spi_dt_spec& Spec() const { return spec_; }

private:
    bool PrepareTx(const uint8_t* data, uint32_t len);
    bool PrepareRx(uint8_t* data, uint32_t len);

    struct spi_dt_spec spec_ {};
    bool               ready_ = false;
    struct spi_buf     tx_buf_ {};
    struct spi_buf     rx_buf_ {};
    struct spi_buf_set tx_set_ {};
    struct spi_buf_set rx_set_ {};
};
