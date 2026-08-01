/**
 * @file spi.hpp
 * @author qingyu
 * @brief SPI 同步收发驱动 — 设备树初始化 + 半双工/全双工
 * @version 0.1
 * @date 2026-06-01
 *
 * # SPI 使用说明
 *
 * 基于 Zephyr SPI API，同步阻塞，不依赖中断或 DMA。
 *
 * ## 设备树
 *
 * ```dts
 * &spi2 {
 *     status = "okay";
 *     imu: imu@0 {
 *         reg = <0>;
 *         spi-max-frequency = <100000>;
 *     };
 * };
 * ```
 *
 * 项目 overlay 中定义 alias：
 * ```dts
 * aliases {
 *     imu-spi = &icm42688p;
 * };
 * ```
 *
 * ### Kconfig
 * ```kconfig
 * config DUST_MOD_DEV_IMU_BMI088
 *     select DUST_COM_SPI
 * ```
 *
 * ### 初始化
 * ```cpp
 * Spi spi{};
 * spi.Init(SPI_DT_SPEC_GET(DT_ALIAS(imu_spi), SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0));
 * ```
 *
 * ### 全双工收发
 * ```cpp
 * uint8_t tx[] = { 0x01, 0x02 };
 * uint8_t rx[2];
 * spi.Transceive(tx, rx, 2);  // 发 tx 同时收 rx
 * ```
 *
 * ### 半双工
 * ```cpp
 * spi.Send(tx_data, len);     // 只写
 * spi.Read(rx_data, len);     // 只读
 * ```
 *
 * ### 总线释放
 * ```cpp
 * spi.Release();  // 控制权交回 DMA 等其他控制器
 * ```
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

/**
 * @brief SPI 同步收发驱动
 *
 * 基于 Zephyr SPI API，支持设备树初始化、半双工读写和全双工收发。
 * 所有操作同步阻塞，不依赖中断或 DMA。
 */
class Spi final
{
public:
    bool Init(const struct spi_dt_spec& spec);

    bool Transceive(const uint8_t* tx_data, uint8_t* rx_data, uint32_t len);
    bool Send(const uint8_t* data, uint32_t len);
    bool Read(uint8_t* data, uint32_t len);
    bool WriteThenRead(const uint8_t* tx_data, uint32_t tx_len, uint8_t* rx_data, uint32_t rx_len);

private:
    bool PrepareTx(const uint8_t* data, uint32_t len);
    bool PrepareRx(uint8_t* data, uint32_t len);

    spi_buf     tx_buf_ {};     // 发送缓冲描述符
    spi_buf_set tx_set_ {};     // 发送缓冲集合

    spi_buf     rx_buf_ {};     // 接收缓冲描述符
    spi_buf_set rx_set_ {};     // 接收缓冲集合

    spi_dt_spec spec_ {};       // 设备树配置
    bool        ready_ = false;
};
