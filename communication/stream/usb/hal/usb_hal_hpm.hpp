/**
 * @file usb_hal_hpm.hpp
 * @author qingyu
 * @brief HPMicro EHCI USB 硬件抽象层
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "../interface/usb_hal.hpp"

/**
 * @brief HPMicro USB 硬件抽象层
 */
class UsbHalHpm final : public UsbHal
{
public:
    UsbHalHpm() = default;

    bool Init(const Config& cfg, EventCallback callback, void* context) override;
    bool Connect() override;
    void Disconnect() override;
    void Deinit() override;
    bool SetAddress(uint8_t address) override;
    usb::Speed GetSpeed() const override;
    bool EpOpen(const usb::EndpointConfig& cfg) override;
    bool EpClose(uint8_t endpoint) override;
    bool EpStall(uint8_t endpoint, bool stall) override;
    bool EpStartRx(uint8_t endpoint, uint16_t length) override;
    bool EpStartTx(uint8_t endpoint, const uint8_t* data, uint16_t length) override;
    bool Ep0StartIn(const uint8_t* data, uint16_t length) override;
    bool Ep0StartOut(uint8_t* data, uint16_t length) override;
    bool Ep0StatusIn() override;
    bool Ep0StatusOut() override;

    void Isr();

private:
    void InitClockAndPhy();
    void ResetController();
    void SetDeviceMode();
    void HandleReset();
    void HandleSetupReceived();
    void HandleTransferComplete(uint32_t edpt_complete);
    uint32_t CalcTransferLength(uint8_t ep_idx, bool* error = nullptr);

    uint8_t  rx_buf_[2][512] {};
    uint8_t  rx_idx_ = 0;
    uint8_t  setup_buf_[8] {};

    EventCallback callback_ = nullptr;
    void*         context_  = nullptr;
    uint32_t      reg_base_ = 0;

    bool ready_ = false;
};
