/**
 * @file usb_function.hpp
 * @author qingyu
 * @brief USB 功能类接口 — UsbDevice 通过此接口调用具体的功能类
 * @version 0.1
 * @date 2026-07-27
 *
 * 从 usb_device.hpp 移出 UsbFunction，
 * 避免 core/ 层依赖具体功能类的实现。
 *
 * 新功能类（HID、MSC、Vendor）只需实现此接口，不改 UsbDevice。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "usb_types.hpp"

/**
 * @brief 端点计划
 *
 * 功能类在 OnConfigured 前通过 GetEndpointPlan 告知 UsbDevice
 * 需要打开的端点列表，由 UsbDevice 统一执行打开和回滚。
 */
struct EndpointPlan {
    struct Entry {
        uint8_t            address   = 0;
        usb::EndpointType  type      = usb::EndpointType::Bulk;
        uint16_t           mps       = 64;
    };

    Entry   entries[5] {};     // 最多 5 个端点
    uint8_t count = 0;         // 实际使用数量
};

/**
 * @brief USB 功能类接口
 *
 * 对应一个 USB 功能（CDC ACM、HID、MSC 等）。
 * UsbDevice 通过此接口与功能类交互，不直接访问具体功能。
 */
class UsbFunction
{
public:
    virtual ~UsbFunction() = default;

    /**
     * @brief 获取描述符
     * @param type    描述符类型
     * @param speed   当前速度
     * @param index   描述符索引（用于字符串/配置）
     * @param length  输出：描述符长度
     * @return 描述符数据指针
     */
    virtual const uint8_t* GetDescriptor(usb::DescriptorType type, usb::Speed speed, uint8_t index, uint16_t& length) const = 0;

    /**
     * @brief 处理 Class 请求
     * @param setup  SETUP 包
     * @param data   数据缓冲区（DATA OUT 时有效）
     * @param length 输入输出：数据长度
     * @return true=已处理，false=不支持（UsbDevice 返回 STALL）
     */
    virtual bool HandleClassRequest(const usb::SetupPacket& setup, uint8_t* data, uint16_t& length) = 0;

    /**
     * @brief 配置状态变化通知
     * @param configured  true=已配置，false=取消配置
     * @return true=配置成功，false=配置失败（UsbDevice 会 STALL）
     */
    virtual bool OnConfigured(bool configured) = 0;

    /**
     * @brief 端点传输完成通知
     * @param endpoint  完成的端点号（含方向）
     * @param data      数据指针
     * @param length    数据长度
     * @param error     true=传输错误（qTD halted/xact_err/buffer_err）
     */
    virtual void OnEndpointComplete(uint8_t endpoint, const uint8_t* data, uint16_t length, bool error = false) = 0;

    /**
     * @brief EP0 DATA OUT 完成回调
     * @param setup  原始 SETUP 包
     * @param data   收到的数据
     * @param length 数据长度
     */
    virtual bool CompleteControlOut(const usb::SetupPacket& setup, const uint8_t* data, uint16_t length) = 0;
};
