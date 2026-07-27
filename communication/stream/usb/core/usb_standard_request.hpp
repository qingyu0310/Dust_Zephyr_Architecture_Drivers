/**
 * @file usb_standard_request.hpp
 * @author qingyu
 * @brief USB 标准请求校验与处理 — 每个请求独立校验
 * @version 0.1
 * @date 2026-07-27
 *
 * 每个请求函数先校验 bmRequestType/wValue/wIndex/wLength/DeviceState，
 * 校验通过后执行并返回 true，校验失败返回 false（调用方负责 STALL）。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "usb_types.hpp"
#include "usb_device.hpp"

/**
 * @brief 标准请求处理器（静态方法集）
 */
class StandardRequestHandler
{
public:
    /**
     * @brief 获取状态（设备/接口/端点）
     */
    static bool HandleGetStatus(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 清除特性（ENDPOINT_HALT/REMOTE_WAKEUP）
     */
    static bool HandleClearFeature(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 设置特性
     */
    static bool HandleSetFeature(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 设置地址
     */
    static bool HandleSetAddress(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 获取描述符
     */
    static bool HandleGetDescriptor(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 设置配置（wValue=0/1）
     */
    static bool HandleSetConfiguration(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 获取当前配置值
     */
    static bool HandleGetConfiguration(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 获取接口备用设置
     */
    static bool HandleGetInterface(const usb::SetupPacket& setup, UsbDevice& device);
    /**
     * @brief 设置接口（wValue=0）
     */
    static bool HandleSetInterface(const usb::SetupPacket& setup, UsbDevice& device);
};
