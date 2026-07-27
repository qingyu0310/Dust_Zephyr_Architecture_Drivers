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
#include "../interface/usb_function.hpp"
#include "usb_control.hpp"

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

    bool        Start()   { return ready_ ? hal_->Connect() : false; }
    void        Stop()    { ready_ = false; hal_->Disconnect(); }
    UsbHal&     GetHal()  { return *hal_; }
    usb::Speed  GetSpeed() const { return speed_; }

private:
    static void HalEvent(void* context, const UsbHal::Event& event);

    void OnEvent(const UsbHal::Event& event);
    void HandleSetup(const uint8_t setup_data[8]);
    void HandleStandardRequest(const usb::SetupPacket& setup);
    void HandleClassRequest(const usb::SetupPacket& setup);
    void HandleTransferComplete(const UsbHal::Event& event);

    void ResetState(){ 
        state_          = usb::DeviceState::Default; 
        address_        = 0; 
        configuration_  = 0; 
        ctrl_.stage     = Ep0Stage::Idle; 
        control_len_    = 0; 
    }

    // 统一 EP0 提交（替代手写 ep0_stage_/Ep0StartIn/返回值检查） =====
    bool SubmitDataIn(const uint8_t* data, uint16_t len);
    bool SubmitDataOut(uint8_t* data, uint16_t len);
    bool SubmitStatusIn();
    bool SubmitStatusOut();

    bool SendDescriptor(const usb::SetupPacket& setup);
    bool SendStatusIn()  { ctrl_.stage = Ep0Stage::StatusIn;  return hal_->Ep0StatusIn(); }
    bool SendStatusOut() { ctrl_.stage = Ep0Stage::StatusOut; return hal_->Ep0StatusOut(); }

    UsbHal*             hal_            = nullptr;
    UsbFunction*        function_       = nullptr;
    usb::DeviceState    state_          = usb::DeviceState::Default;
    uint8_t             address_        = 0;
    uint8_t             configuration_  = 0;
    usb::Speed          speed_          = usb::Speed::Full;

    /// EP0 控制传输上下文（含 stage/sequence/分段状态）
    ControlTransfer  ctrl_ {};

    /// 当前 class 请求 SETUP 包（用于 DATA OUT 完成后的回调）
    usb::SetupPacket class_setup_ {};
    bool ready_ = false;

    // EP0 控制传输数据缓冲（GET_DESCRIPTOR 外的数据收发）
    // cacheline 对齐——DMA 和 cache 操作需要
    alignas(32) uint8_t control_buffer_[512] {};
    uint16_t control_len_  = 0;
};
