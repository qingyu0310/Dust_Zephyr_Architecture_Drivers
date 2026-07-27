/**
 * @file usb_descriptor.cpp
 * @author qingyu
 * @brief USB 描述符生成实现
 * @version 0.1
 * @date 2026-07-27
 *
 * 所有描述符数据手动填入 uint8_t 数组，
 * 不使用 struct packing、不使用 CherryUSB 宏。
 * 避免编译器 padding、大小端、不同 MCU ABI 差异。
 *
 * @copyright Copyright (c) 2026
 */

#include "usb_descriptor.hpp"
#include <string.h>

/**
 * @brief 将 ASCII 字符串写入 UTF-16LE 描述符格式
 * @param buf    输出缓冲区
 * @param text   ASCII 输入（仅支持 7-bit ASCII）
 * @return 写入的总字节数（含 2 字节头）
 */
uint16_t UsbDescriptorSet::WriteAsciiUtf16le(uint8_t* buf, const char* text)
{
    if (buf == nullptr || text == nullptr) {
        return 0;
    }

    size_t ascii_len = strlen(text);
    if (ascii_len > 126) {
        ascii_len = 126;                                        // USB 字符串描述符最多 126 个字符
    }

    uint16_t total_len = static_cast<uint16_t>(2 + ascii_len * 2);
    buf[0] = static_cast<uint8_t>(total_len);                   // 长度
    buf[1] = 0x03;                                              // 类型 = 字符串描述符

    for (size_t i = 0; i < ascii_len; i++) {
        buf[2 + i * 2]     = static_cast<uint8_t>(text[i]);     // UTF-16LE 低字节
        buf[2 + i * 2 + 1] = 0x00;                              // UTF-16LE 高字节（ASCII 为 0）
    }

    return total_len;
}

/**
 * @brief 构建设备描述符（18 字节）
 * @param vid      厂商 ID
 * @param pid      产品 ID
 * @param bcd_device 设备版本号
 * @param mfr_idx  厂商字符串索引
 * @param prod_idx 产品字符串索引
 * @param ser_idx  序列号字符串索引
 */
void UsbDescriptorSet::BuildDevice(uint16_t vid, uint16_t pid, uint16_t bcd_device, uint8_t mfr_idx, uint8_t prod_idx, uint8_t ser_idx)
{
    uint8_t d[18] {};
    d[0]  = 18;                                             // 描述符长度
    d[1]  = 1;                                              // 描述符类型 = 设备
    d[2]  = 0x00;                                           // USB 版本（小端）
    d[3]  = 0x02;                                           //   = 0x0200
    d[4]  = 0xEF;                                           // 设备类 = MISC
    d[5]  = 2;                                              // 设备子类
    d[6]  = 1;                                              // 设备协议
    d[7]  = 64;                                             // 端点 0 最大包大小
    d[8]  = static_cast<uint8_t>(vid & 0xFF);               // 厂商 ID
    d[9]  = static_cast<uint8_t>((vid >> 8) & 0xFF);
    d[10] = static_cast<uint8_t>(pid & 0xFF);               // 产品 ID
    d[11] = static_cast<uint8_t>((pid >> 8) & 0xFF);
    d[12] = static_cast<uint8_t>(bcd_device & 0xFF);        // 设备版本
    d[13] = static_cast<uint8_t>((bcd_device >> 8) & 0xFF);
    d[14] = mfr_idx;                                        // 厂商字符串索引
    d[15] = prod_idx;                                       // 产品字符串索引
    d[16] = ser_idx;                                        // 序列号字符串索引
    d[17] = 1;                                              // 配置数量

    memcpy(device_desc_, d, sizeof(d));
}

/**
 * @brief 构建配置描述符（含 CDC ACM 复合描述符）
 * @param buf  输出缓冲区
 * @param mps  批量端点最大包大小（FS=64, HS=512）
 */
void UsbDescriptorSet::BuildConfig(uint8_t* buf, uint16_t mps)
{
    uint16_t total_len = kUsbConfigTotalLen;

    // 配置描述符头（9 字节）
    buf[0] = 9;                                             // 描述符长度
    buf[1] = 2;                                             // 描述符类型 = 配置
    buf[2] = static_cast<uint8_t>(total_len & 0xFF);        // 总长度（小端）
    buf[3] = static_cast<uint8_t>((total_len >> 8) & 0xFF);
    buf[4] = 2;                                             // 接口数量
    buf[5] = 1;                                             // 配置值
    buf[6] = 0;                                             // 配置字符串索引
    buf[7] = 0x80;                                          // 属性（自供电，无远程唤醒）
    buf[8] = 100;                                           // 最大功耗（200mA / 2）

    uint16_t pos = 9;

    // 接口关联描述符 IAD（8 字节）
    buf[pos + 0] = 8;                                       // 描述符长度
    buf[pos + 1] = 11;                                      // 描述符类型 = IAD
    buf[pos + 2] = 0;                                       // 首个接口号
    buf[pos + 3] = 2;                                       // 接口数量
    buf[pos + 4] = 2;                                       // 功能类 = CDC
    buf[pos + 5] = 2;                                       // 功能子类 = ACM
    buf[pos + 6] = 0;                                       // 功能协议 = 无协议（与 CherryUSB CDC_ACM_DESCRIPTOR_INIT 对齐）
    buf[pos + 7] = 0;                                       // 功能字符串索引
    pos += 8;

    // CDC 控制接口（9 字节）
    buf[pos + 0] = 9;                                       // 描述符长度
    buf[pos + 1] = 4;                                       // 描述符类型 = 接口
    buf[pos + 2] = 0;                                       // 接口号（控制接口）
    buf[pos + 3] = 0;                                       // 备用设置
    buf[pos + 4] = 1;                                       // 端点数量（仅中断 IN）
    buf[pos + 5] = 2;                                       // 接口类 = CDC
    buf[pos + 6] = 2;                                       // 接口子类 = ACM
    buf[pos + 7] = 0;                                       // 接口协议 = 无协议（与 CherryUSB CDC_ACM_DESCRIPTOR_INIT 对齐）
    buf[pos + 8] = 0;                                       // 接口字符串索引
    pos += 9;

    // 头功能描述符（5 字节）
    buf[pos + 0] = 5;                                       // 描述符长度
    buf[pos + 1] = 0x24;                                    // 描述符类型 = CS_INTERFACE
    buf[pos + 2] = 0;                                       // 子类型 = 头
    buf[pos + 3] = 0x10;                                    // CDC 版本（小端）
    buf[pos + 4] = 0x01;                                    //   = 0x0110
    pos += 5;

    // 呼叫管理功能描述符（5 字节）
    buf[pos + 0] = 5;                                       // 描述符长度
    buf[pos + 1] = 0x24;                                    // 描述符类型 = CS_INTERFACE
    buf[pos + 2] = 1;                                       // 子类型 = 呼叫管理
    buf[pos + 3] = 0x00;                                    // 能力（不处理呼叫管理）
    buf[pos + 4] = 1;                                       // 数据接口号 = 1
    pos += 5;

    // 抽象控制管理描述符（4 字节）
    buf[pos + 0] = 4;                                       // 描述符长度
    buf[pos + 1] = 0x24;                                    // 描述符类型 = CS_INTERFACE
    buf[pos + 2] = 2;                                       // 子类型 = 抽象控制管理
    buf[pos + 3] = 0x06;                                    // 能力：SET/GET_LINE_CODING + SET_CONTROL_LINE_STATE + SEND_BREAK
    pos += 4;

    // 联合功能描述符（5 字节）
    buf[pos + 0] = 5;                                       // 描述符长度
    buf[pos + 1] = 0x24;                                    // 描述符类型 = CS_INTERFACE
    buf[pos + 2] = 6;                                       // 子类型 = 联合
    buf[pos + 3] = 0;                                       // 主接口 = 0（控制）
    buf[pos + 4] = 1;                                       // 从接口 = 1（数据）
    pos += 5;

    // 中断 IN 端点（7 字节）
    buf[pos + 0] = 7;                                       // 描述符长度
    buf[pos + 1] = 5;                                       // 描述符类型 = 端点
    buf[pos + 2] = 0x83;                                    // 端点地址（IN, ep3）
    buf[pos + 3] = 3;                                       // 属性 = 中断
    buf[pos + 4] = 8;                                       // 最大包大小（小端）
    buf[pos + 5] = 0;                                       //   = 8
    buf[pos + 6] = 10;                                      // 轮询间隔（与 CherryUSB CDC_ACM_DESCRIPTOR_INIT 对齐）
    pos += 7;

    // CDC 数据接口（9 字节）
    buf[pos + 0] = 9;                                       // 描述符长度
    buf[pos + 1] = 4;                                       // 描述符类型 = 接口
    buf[pos + 2] = 1;                                       // 接口号（数据接口）
    buf[pos + 3] = 0;                                       // 备用设置
    buf[pos + 4] = 2;                                       // 端点数量（批量 OUT + 批量 IN）
    buf[pos + 5] = 0x0A;                                    // 接口类 = 数据
    buf[pos + 6] = 0;                                       // 接口子类
    buf[pos + 7] = 0;                                       // 接口协议
    buf[pos + 8] = 0;                                       // 接口字符串索引
    pos += 9;

    // 批量 OUT 端点（7 字节）
    buf[pos + 0] = 7;                                       // 描述符长度
    buf[pos + 1] = 5;                                       // 描述符类型 = 端点
    buf[pos + 2] = 0x01;                                    // 端点地址（OUT, ep1）
    buf[pos + 3] = 2;                                       // 属性 = 批量
    buf[pos + 4] = static_cast<uint8_t>(mps & 0xFF);        // 最大包大小（小端）
    buf[pos + 5] = static_cast<uint8_t>((mps >> 8) & 0xFF);
    buf[pos + 6] = 0;                                       // 轮询间隔
    pos += 7;

    // 批量 IN 端点（7 字节）
    buf[pos + 0] = 7;                                       // 描述符长度
    buf[pos + 1] = 5;                                       // 描述符类型 = 端点
    buf[pos + 2] = 0x81;                                    // 端点地址（IN, ep1）
    buf[pos + 3] = 2;                                       // 属性 = 批量
    buf[pos + 4] = static_cast<uint8_t>(mps & 0xFF);        // 最大包大小（小端）
    buf[pos + 5] = static_cast<uint8_t>((mps >> 8) & 0xFF);
    buf[pos + 6] = 0;                                       // 轮询间隔
}

/**
 * @brief 构建设备限定符描述符（10 字节）
 */
void UsbDescriptorSet::BuildQualifier()
{
    uint8_t d[10] {};
    d[0] = 10;          // 描述符长度
    d[1] = 6;           // 描述符类型 = 设备限定符
    d[2] = 0x00;        // USB 版本（小端）
    d[3] = 0x02;        //   = 0x0200
    d[4] = 0xEF;        // 设备类 = MISC
    d[5] = 2;           // 设备子类
    d[6] = 1;           // 设备协议
    d[7] = 64;          // 端点 0 最大包大小
    d[8] = 1;           // 配置数量
    d[9] = 0;           // 保留

    memcpy(qualifier_, d, sizeof(d));
}

/**
 * @brief 构建其他速度配置描述符
 * @param buf  输出缓冲区
 * @param mps  另一速度下的批量端点最大包大小
 *
 * 和配置描述符布局相同，仅描述符类型字节不同。
 * 当前 HS 时返回 FS 的其他速度配置，当前 FS 时反之。
 */
void UsbDescriptorSet::BuildOtherSpeed(uint8_t* buf, uint16_t mps)
{
    BuildConfig(buf, mps);
    buf[1] = 7;  // 描述符类型 = 其他速度配置
}

/**
 * @brief 构建单个字符串描述符（UTF-16LE）
 * @param index 字符串索引
 * @param text  ASCII 输入
 */
void UsbDescriptorSet::BuildString(uint8_t index, const char* text)
{
    if (index >= 4 || text == nullptr) {
        return;
    }
    WriteAsciiUtf16le(string_desc_[index], text);
    if (index + 1 > string_count_) {
        string_count_ = static_cast<uint8_t>(index + 1);
    }
}

/**
 * @brief 构建所有字符串描述符（语言 ID + 厂商 + 产品 + 序列号）
 * @param cfg  描述符配置
 */
void UsbDescriptorSet::BuildStrings(const Config& cfg)
{
    // 索引 0 — 语言 ID
    string_desc_[0][0] = 4;     // 长度
    string_desc_[0][1] = 3;     // 类型 = 字符串
    string_desc_[0][2] = 0x09;  // LANGID 低字节 = English
    string_desc_[0][3] = 0x04;  // LANGID 高字节 = US
    string_count_ = 1;

    // 索引 1 — 厂商
    BuildString(cfg.manufacturer_index, cfg.manufacturer);
    // 索引 2 — 产品
    BuildString(cfg.product_index,      cfg.product);
    // 索引 3 — 序列号
    BuildString(cfg.serial_index,       cfg.serial_number);
}

/**
 * @brief 构造并生成所有描述符
 * @param cfg  描述符配置
 */
UsbDescriptorSet::UsbDescriptorSet(const Config& cfg)
{
    BuildDevice(cfg.vid, cfg.pid, cfg.bcd_device,
                cfg.manufacturer_index, cfg.product_index, cfg.serial_index);
    BuildConfig(config_fs_, 64);   // FS 批量端点 MPS = 64
    BuildConfig(config_hs_, 512);  // HS 批量端点 MPS = 512
    BuildQualifier();
    BuildOtherSpeed(other_speed_fs_, 64);   // HS 运行时返回 FS 配置
    BuildOtherSpeed(other_speed_hs_, 512);  // FS 运行时返回 HS 配置
    BuildStrings(cfg);
}

/**
 * @brief 获取设备描述符
 * @param length  输出：描述符字节数
 * @return 描述符数据指针
 */
const uint8_t* UsbDescriptorSet::GetDeviceDescriptor(uint16_t& length) const
{
    length = sizeof(device_desc_);
    return device_desc_;
}

/**
 * @brief 获取配置描述符（根据速度选择 FS/HS）
 * @param speed   当前 USB 速度
 * @param length  输出：描述符字节数
 * @return 描述符数据指针
 */
const uint8_t* UsbDescriptorSet::GetConfigurationDescriptor(usb::Speed speed, uint16_t& length) const
{
    if (speed == usb::Speed::High) {
        length = sizeof(config_hs_);
        return config_hs_;
    }
    length = sizeof(config_fs_);
    return config_fs_;
}

/**
 * @brief 获取设备限定符描述符
 * @param length  输出：描述符字节数
 * @return 描述符数据指针
 */
const uint8_t* UsbDescriptorSet::GetQualifierDescriptor(uint16_t& length) const
{
    length = sizeof(qualifier_);
    return qualifier_;
}

/**
 * @brief 获取其他速度配置描述符
 * @param speed   当前 USB 速度（HS 则返回 FS 配置，FS 则返回 HS 配置）
 * @param length  输出：描述符字节数
 * @return 描述符数据指针
 */
const uint8_t* UsbDescriptorSet::GetOtherSpeedDescriptor(usb::Speed speed, uint16_t& length) const
{
    if (speed == usb::Speed::High) {
        length = sizeof(other_speed_fs_);
        return other_speed_fs_;
    }
    length = sizeof(other_speed_hs_);
    return other_speed_hs_;
}

/**
 * @brief 获取字符串描述符
 * @param index   字符串索引（0=语言ID, 1=厂商, 2=产品, 3=序列号）
 * @param length  输出：描述符字节数
 * @return 描述符数据指针，越界返回 nullptr
 */
const uint8_t* UsbDescriptorSet::GetStringDescriptor(uint8_t index, uint16_t& length) const
{
    if (index >= string_count_) {
        length = 0;
        return nullptr;
    }

    length = string_desc_[index][0];  // 首字节即长度
    return string_desc_[index];
}
