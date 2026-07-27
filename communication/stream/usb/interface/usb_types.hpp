/**
 * @file usb_types.hpp
 * @author qingyu
 * @brief USB 协议基础类型，不依赖任何硬件和协议栈
 * @version 0.1
 * @date 2026-07-27
 *
 * 只放 USB 规范定义的数据类型和小型工具函数。
 * 不放控制器寄存器、不放描述符生成逻辑、不放任何 CherryUSB 引用。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

namespace usb {

/**
 * @brief USB 速度
 */
enum class Speed : uint8_t {
    Full = 0,
    High = 1,
};

/**
 * @brief USB 端点类型
 */
enum class EndpointType : uint8_t {
    Control     = 0,
    Isochronous = 1,
    Bulk        = 2,
    Interrupt   = 3,
};

/**
 * @brief USB 设备状态
 */
enum class DeviceState : uint8_t {
    Default,
    Addressed,
    Configured,
    Suspended,
};

/**
 * @brief USB 描述符类型
 */
enum class DescriptorType : uint8_t {
    Device                   = 1,
    Configuration            = 2,
    String                   = 3,
    Interface                = 4,
    Endpoint                 = 5,
    DeviceQualifier          = 6,
    OtherSpeedConfiguration  = 7,
    InterfacePower           = 8,
};

/**
 * @brief USB 标准请求
 */
enum class StandardRequest : uint8_t {
    GetStatus        = 0,
    ClearFeature     = 1,
    SetFeature       = 3,
    SetAddress       = 5,
    GetDescriptor    = 6,
    SetDescriptor    = 7,
    GetConfiguration = 8,
    SetConfiguration = 9,
    GetInterface     = 10,
    SetInterface     = 11,
    SynchFrame       = 12,
};

/**
 * @brief USB 特性选择子
 */
enum class FeatureSelector : uint8_t {
    EndpointHalt       = 0,
    DeviceRemoteWakeup = 1,
    TestMode           = 2,
};

/**
 * @brief bmRequestType 方向掩码
 */
static constexpr uint8_t kDirectionMask    = 0x80;
static constexpr uint8_t kDirectionHostToDevice = 0x00;
static constexpr uint8_t kDirectionDeviceToHost = 0x80;

/**
 * @brief bmRequestType 类型掩码
 */
static constexpr uint8_t kTypeMask      = 0x60;
static constexpr uint8_t kTypeStandard  = 0x00;
static constexpr uint8_t kTypeClass     = 0x20;
static constexpr uint8_t kTypeVendor    = 0x40;

/**
 * @brief bmRequestType 接收者掩码
 */
static constexpr uint8_t kRecipientMask      = 0x1F;
static constexpr uint8_t kRecipientDevice    = 0x00;
static constexpr uint8_t kRecipientInterface = 0x01;
static constexpr uint8_t kRecipientEndpoint  = 0x02;
static constexpr uint8_t kRecipientOther     = 0x03;

/**
 * @brief 端点方向
 */
static constexpr uint8_t kEpDirOut = 0x00;
static constexpr uint8_t kEpDirIn  = 0x80;

/**
 * @brief 端点配置
 */
struct EndpointConfig {
    uint8_t      address        = 0;
    EndpointType type           = EndpointType::Bulk;
    uint16_t     max_packet_size = 64;
    uint8_t      interval       = 0;
};

/**
 * @brief SETUP 包
 */
struct SetupPacket {
    uint8_t  bm_request_type = 0;
    uint8_t  b_request       = 0;
    uint16_t w_value         = 0;
    uint16_t w_index         = 0;
    uint16_t w_length        = 0;

    bool IsDeviceToHost() const
    {
        return (bm_request_type & kDirectionMask) == kDirectionDeviceToHost;
    }
    bool IsStandard() const
    {
        return (bm_request_type & kTypeMask) == kTypeStandard;
    }
    bool IsClass() const
    {
        return (bm_request_type & kTypeMask) == kTypeClass;
    }
    bool IsVendor() const
    {
        return (bm_request_type & kTypeMask) == kTypeVendor;
    }
    uint8_t Recipient() const
    {
        return bm_request_type & kRecipientMask;
    }
};

/**
 * @brief CDC ACM 线编码
 *
 * 手工序列化 7 字节，不使用 packed 结构体，
 * 避免编译器 padding 和大小端差异。
 */
struct LineCoding {
    uint32_t dte_rate    = 115200;
    uint8_t  char_format = 0;     // 0=1 stop, 1=1.5, 2=2
    uint8_t  parity_type = 0;     // 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
    uint8_t  data_bits   = 8;
};

/**
 * @brief 将 LineCoding 序列化为 7 字节 USB 线编码格式（小端）
 * @param value  输入
 * @param out    输出缓冲区（必须 >= 7 字节）
 */
inline void EncodeLineCoding(const LineCoding& value, uint8_t out[7])
{
    // dwDTERate — 小端 uint32_t
    out[0] = static_cast<uint8_t>(value.dte_rate & 0xFF);
    out[1] = static_cast<uint8_t>((value.dte_rate >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((value.dte_rate >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((value.dte_rate >> 24) & 0xFF);
    // bCharFormat
    out[4] = value.char_format;
    // bParityType
    out[5] = value.parity_type;
    // bDataBits
    out[6] = value.data_bits;
}

/**
 * @brief 将 7 字节 USB 线编码反序列化为 LineCoding
 * @param in   输入缓冲区（必须 >= 7 字节）
 * @param out  输出
 * @return true=成功
 */
inline bool DecodeLineCoding(const uint8_t in[7], LineCoding& out)
{
    if (in == nullptr) {
        return false;
    }

    out.dte_rate    =  static_cast<uint32_t>(in[0])
                    | (static_cast<uint32_t>(in[1]) << 8)
                    | (static_cast<uint32_t>(in[2]) << 16)
                    | (static_cast<uint32_t>(in[3]) << 24);
    out.char_format  = in[4];
    out.parity_type  = in[5];
    out.data_bits    = in[6];
    return true;
}

}  // namespace usb
