/**
 * @file usb_descriptor.hpp
 * @author qingyu
 * @brief USB 描述符生成器 — 手动生成 uint8_t 数组，不依赖 CherryUSB 宏
 * @version 0.1
 * @date 2026-07-27
 *
 * 不再使用 CherryUSB 的 USB_DEVICE_DESCRIPTOR_INIT / CDC_ACM_DESCRIPTOR_INIT 等宏。
 * 改用显式的 constexpr uint8_t 数组或构造时填充，消除对 CherryUSB 头文件的依赖。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "usb_types.hpp"

/**
 * @brief CDC ACM 复合描述符总长度（不含配置描述头）
 *
 * 组成：
 *   Interface Association Descriptor  8
 *   CDC Control Interface             9
 *     Header Functional               5
 *     Call Management Functional      5
 *     Abstract Control Management     4
 *     Union Functional                5
 *     Interrupt IN Endpoint           7
 *   CDC Data Interface                9
 *     Bulk OUT Endpoint               7
 *     Bulk IN Endpoint                7
 *   ───────────────────────────────
 *   总计                             66
 */
static constexpr uint16_t kCdcAcmDescriptorLen = 66;

/**
 * @brief USB 配置描述符总长：标准配置头 + CDC ACM 复合描述符
 */
static constexpr uint16_t kUsbConfigTotalLen = 9 + kCdcAcmDescriptorLen;

/**
 * @brief 描述符集合
 *
 * 管理所有 USB 描述符的生成和查询。
 * 构造时生成所有描述符，后续只读查询。
 */
class UsbDescriptorSet final
{
public:
    /**
     * @brief 描述符配置
     */
    struct Config {
        uint16_t    vid                 = 0x34B7;           // 厂商 ID
        uint16_t    pid                 = 0xFFFF;           // 产品 ID
        uint16_t    bcd_device          = 0x0100;           // 设备版本号
        uint8_t     manufacturer_index  = 1;                // 厂商字符串索引
        uint8_t     product_index       = 2;                // 产品字符串索引
        uint8_t     serial_index        = 3;                // 序列号字符串索引
        const char* manufacturer        = "MCHCK";          // 厂商名
        const char* product             = "USB CDC ACM";    // 产品名
        const char* serial_number       = "qingyu_king";    // 序列号
    };

    /**
     * @brief 构造并生成所有描述符
     * @param cfg  描述符配置
     */
    explicit UsbDescriptorSet(const Config& cfg);

    const uint8_t* GetDeviceDescriptor(uint16_t& length) const;
    const uint8_t* GetConfigurationDescriptor(usb::Speed speed, uint16_t& length) const;
    const uint8_t* GetQualifierDescriptor(uint16_t& length) const;
    const uint8_t* GetOtherSpeedDescriptor(usb::Speed speed, uint16_t& length) const;
    const uint8_t* GetStringDescriptor(uint8_t index, uint16_t& length) const;

private:
    static constexpr uint16_t kMaxStringLen = 32;

    // 设备描述符 18 字节
    uint8_t device_desc_[18] {};

    // 配置描述符（FS: bulk MPS=64, HS: bulk MPS=512）
    uint8_t config_fs_[kUsbConfigTotalLen] {};
    uint8_t config_hs_[kUsbConfigTotalLen] {};

    // 设备限定符 10 字节
    uint8_t qualifier_[10] {};

    // 其他速度配置描述符（FS 的 other-speed 用 HS MPS，反之亦然）
    uint8_t other_speed_fs_[kUsbConfigTotalLen] {};
    uint8_t other_speed_hs_[kUsbConfigTotalLen] {};

    // 字符串描述符（索引 0=语言 ID, 1=厂商, 2=产品, 3=序列号）
    uint8_t string_desc_[4][kMaxStringLen] {};
    uint8_t string_count_ = 0;

    void BuildDevice(uint16_t vid, uint16_t pid, uint16_t bcd_device, uint8_t mfr_idx, uint8_t prod_idx, uint8_t ser_idx);
    void BuildConfig(uint8_t* buf, uint16_t mps);
    void BuildQualifier();
    void BuildOtherSpeed(uint8_t* buf, uint16_t mps);
    void BuildString(uint8_t index, const char* text);
    void BuildStrings(const Config& cfg);
    
    static uint16_t WriteAsciiUtf16le(uint8_t* buf, const char* text);
};
