/**
 * @file usb_device.hpp
 * @author qingyu
 * @brief USB 设备核心层 — EP0 控制传输 + 标准请求处理
 * @version 0.1
 * @date 2026-07-27
 *
 * UsbDevice 对应 CherryUSB 的 usbd_core 层。
 * 负责：
 *   - USB device 状态管理
 *   - EP0 控制传输状态机
 *   - 标准请求处理（GET_DESCRIPTOR、SET_ADDRESS、SET_CONFIGURATION 等）
 *   - Class 请求分发
 *   - Endpoint complete 分发
 *   - Reset/Disconnect 统一复位
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "usb_hal.hpp"
#include "usb_types.hpp"

/**
 * @brief USB 功能类接口
 *
 * UsbDevice 通过此接口调用具体的功能类（如 UsbCdcAcm）。
 * 添加新类（HID、MSC、Vendor）时只需实现此接口，不改 UsbDevice。
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
    virtual const uint8_t* GetDescriptor(usb::DescriptorType type,
                                         usb::Speed speed,
                                         uint8_t index,
                                         uint16_t& length) const = 0;

    /**
     * @brief 处理 Class 请求
     * @param setup  SETUP 包
     * @param data   数据缓冲区（DATA OUT 时有效）
     * @param length 输入输出：数据长度
     * @return true=已处理，false=不支持（UsbDevice 返回 STALL）
     */
    virtual bool HandleClassRequest(const usb::SetupPacket& setup,
                                    uint8_t* data,
                                    uint16_t& length) = 0;

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
    virtual void OnEndpointComplete(uint8_t endpoint,
                                    const uint8_t* data,
                                    uint16_t length,
                                    bool error = false) = 0;

    /**
     * @brief EP0 DATA OUT 阶段完成（用于 SETUP 类的 DATA OUT 收尾）
     * @param setup  原始 SETUP 包
     * @param data   收到的数据
     * @param length 数据长度
     */
    virtual bool CompleteControlOut(const usb::SetupPacket& setup,
                                     const uint8_t* data,
                                     uint16_t length) { return true; }
};

/**
 * @brief USB 设备核心
 */
class UsbDevice final
{
public:
    /**
     * @brief 初始化
     * @param hal      HAL 实例
     * @param function 功能类（第一版为 UsbCdcAcm）
     * @param cfg      HAL 配置
     * @return true=成功
     */
    bool Init(UsbHal& hal, UsbFunction& function, const UsbHal::Config& cfg);

    /**
     * @brief 启动 USB
     */
    bool Start();

    /**
     * @brief 停止 USB
     */
    void Stop();

    bool         IsReady()       const { return ready_; }
    bool         IsConfigured()  const { return state_ == usb::DeviceState::Configured; }
    usb::Speed   GetSpeed()      const { return speed_; }
    UsbHal&      GetHal()              { return *hal_; }

private:
    static void HalEvent(void* context, const UsbHal::Event& event);

    void OnEvent(const UsbHal::Event& event);
    void HandleSetup(const uint8_t setup_data[8]);
    void HandleStandardRequest(const usb::SetupPacket& setup);
    void HandleClassRequest(const usb::SetupPacket& setup);
    void HandleTransferComplete(const UsbHal::Event& event);
    void ResetState();

    bool SendDescriptor(const usb::SetupPacket& setup);
    bool SendStatusIn();
    bool SendStatusOut();

    /// EP0 控制传输阶段状态
    enum class Ep0Stage : uint8_t {
        Idle,
        DataIn,
        DataOut,
        StatusIn,
        StatusOut,
    };

    UsbHal*       hal_      = nullptr;
    UsbFunction*  function_ = nullptr;
    usb::DeviceState  state_    = usb::DeviceState::Default;
    uint8_t       address_  = 0;
    uint8_t       configuration_   = 0;
    usb::Speed    speed_    = usb::Speed::Full;
    Ep0Stage      ep0_stage_  = Ep0Stage::Idle;

    /// 当前 class 请求 SETUP 包（用于 DATA OUT 完成后的回调）
    usb::SetupPacket class_setup_ {};
    bool          ready_    = false;

    /// EP0 控制传输数据缓冲（GET_DESCRIPTOR 外的数据收发）
    /// cacheline 对齐——DMA 和 cache 操作需要
    alignas(32) uint8_t control_buffer_[512] {};
    uint16_t control_len_  = 0;
};
