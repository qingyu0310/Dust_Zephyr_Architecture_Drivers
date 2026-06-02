/**
 * @file spi.cpp
 * @author qingyu
 * @brief SPI driver implementation
 * @version 0.1
 * @date 2026-06-01
 */

#include "spi.hpp"

bool Spi::Init(const struct spi_dt_spec& spec)
{
    if (!spi_is_ready_dt(&spec)) {
        return false;
    }

    spec_  = spec;
    ready_ = true;
    tx_set_.buffers = &tx_buf_;
    tx_set_.count   = 1;
    rx_set_.buffers = &rx_buf_;
    rx_set_.count   = 1;
    return true;
}

bool Spi::Transceive(const struct spi_buf_set* tx, const struct spi_buf_set* rx) const
{
    if (!ready_) return false;
    return spi_transceive_dt(&spec_, tx, rx) == 0;
}

bool Spi::Send(const struct spi_buf_set* tx) const
{
    if (!ready_) return false;
    return spi_write_dt(&spec_, tx) == 0;
}

bool Spi::Read(const struct spi_buf_set* rx) const
{
    if (!ready_) return false;
    return spi_read_dt(&spec_, rx) == 0;
}

bool Spi::Transceive(const uint8_t* tx_data, uint8_t* rx_data, uint32_t len)
{
    if (!PrepareTx(tx_data, len) || !PrepareRx(rx_data, len)) {
        return false;
    }

    return Transceive(&tx_set_, &rx_set_);
}

bool Spi::Send(const uint8_t* data, uint32_t len)
{
    if (!PrepareTx(data, len)) {
        return false;
    }

    return Send(&tx_set_);
}

bool Spi::Read(uint8_t* data, uint32_t len)
{
    if (!PrepareRx(data, len)) {
        return false;
    }

    return Read(&rx_set_);
}

bool Spi::Release() const
{
    if (!ready_) return false;
    return spi_release_dt(&spec_) == 0;
}

bool Spi::IsReady() const
{
    return ready_ && spi_is_ready_dt(&spec_);
}

bool Spi::PrepareTx(const uint8_t* data, uint32_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    tx_buf_.buf = const_cast<uint8_t*>(data);
    tx_buf_.len = len;
    return true;
}

bool Spi::PrepareRx(uint8_t* data, uint32_t len)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    rx_buf_.buf = data;
    rx_buf_.len = len;
    return true;
}
