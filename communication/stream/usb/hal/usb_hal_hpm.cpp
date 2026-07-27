/**
 * @file usb_hal_hpm.cpp
 * @author qingyu
 * @brief HPMicro EHCI USB 硬件抽象层实现
 * @version 0.1
 * @date 2026-07-27
 *
 * @copyright Copyright (c) 2026
 */

#include "usb_hal_hpm.hpp"

#include <string.h>

#include <zephyr/logging/log.h>

#include <hpm_soc.h>
#include <hpm_clock_drv.h>
#include <hpm_usb_drv.h>
#include <hpm_usb_device.h>
#include <hpm_l1c_drv.h>
#include <hpm_common.h>
#include <dt-bindings/clock/hpm5361-clocks.h>

LOG_MODULE_REGISTER(usb_hal_hpm, LOG_LEVEL_INF);

static constexpr uint32_t kIntrUsb        = USB_USBINTR_UE_MASK;
static constexpr uint32_t kIntrError      = USB_USBINTR_UEE_MASK;
static constexpr uint32_t kIntrPortChange = USB_USBINTR_PCE_MASK;
static constexpr uint32_t kIntrReset      = USB_USBINTR_URE_MASK;
static constexpr uint32_t kIntrSuspend    = USB_USBINTR_SLE_MASK;

// Nocache 资源
// dcd_data_t 包含 QHD/QTD，HPM5361 要求 ENDPTLISTADDR 按 2048 对齐
// 必须同时按 SoC 要求对齐起始地址和对象尺寸，和 CherryUSB 保持一致。
__attribute__((section(".nocache"), aligned(USB_SOC_DCD_DATA_RAM_ADDRESS_ALIGNMENT)))
static uint8_t s_dcd_data[
    HPM_ALIGN_UP(sizeof(dcd_data_t), USB_SOC_DCD_DATA_RAM_ADDRESS_ALIGNMENT)];
__attribute__((section(".nocache"))) static usb_device_handle_t  s_handle;
__attribute__((section(".nocache"), aligned(HPM_L1C_CACHELINE_SIZE))) static uint8_t s_rx_buf[2][512];
__attribute__((section(".nocache"), aligned(HPM_L1C_CACHELINE_SIZE))) static uint8_t s_tx_buf[512];
__attribute__((section(".nocache"), aligned(HPM_L1C_CACHELINE_SIZE))) static uint8_t s_setup_buf[8];

// ISR 入口由 HAL 在 Init() 中动态连接。
// 使用静态指针路由到当前实例，避免 C IRQ 包装层持有失效对象。
static UsbHalHpm* s_isr_hal = nullptr;

extern "C" void usb_isr_entry(const void*)
{
    if (s_isr_hal) {
        s_isr_hal->Isr();
    }
}

// 端点状态跟踪（对应 CherryUSB 的 in_ep/out_ep）
static struct {
    uint8_t* buf;
    uint16_t len;
    bool     enable;
} s_in_ep[16], s_out_ep[16];

bool UsbHalHpm::Init(const Config& cfg, EventCallback callback, void* context)
{
    if (ready_)
        return true;

    reg_base_ = cfg.reg_base;
    callback_ = callback;
    context_  = context;

    InitClockAndPhy();

    // 先设置 ISR 路由，再连接 IRQ；ISR 入口不使用 irq_connect_dynamic 的 arg。
    s_isr_hal = this;

    // IRQ 连接（注册 ISR 到中断向量表）
    if (irq_connect_dynamic(cfg.irq_num, cfg.irq_priority, usb_isr_entry, this, 0) < 0) {
        LOG_ERR("IRQ connect failed (irq=%u)", cfg.irq_num);
        return false;
    }

    // 先初始化 handle，再 usb_device_init
    USB_Type* regs = (USB_Type*)reg_base_;
    memset(&s_handle, 0, sizeof(s_handle));
    s_handle.regs     = regs;
    s_handle.dcd_data = (dcd_data_t*)s_dcd_data;

    uint32_t mask = kIntrUsb | kIntrError | kIntrPortChange | kIntrReset | kIntrSuspend;
    if (!usb_device_init(&s_handle, mask)) {
        LOG_ERR("usb_device_init failed");
        return false;
    }

    LOG_INF("DCD local=%p sys=0x%08x align=%u size=%u ENDPTLISTADDR=0x%08x",
            s_dcd_data,
            core_local_mem_to_sys_address(0, (uint32_t)s_dcd_data),
            USB_SOC_DCD_DATA_RAM_ADDRESS_ALIGNMENT,
            (unsigned)sizeof(s_dcd_data),
            (unsigned)s_handle.regs->ENDPTLISTADDR);

    // usb_device_init 已经打开控制器中断，必须先进入可处理状态再放行 IRQ。
    ready_ = true;

    // 开启 CPU 级 USB 中断
    // CherryUSB 在 usb_dc_init → usb_dc_isr_connect → intc_m_enable_irq 中做
    // irq_connect_dynamic 只注册了 ISR，不会使能中断，必须显式调 irq_enable
    irq_enable(cfg.irq_num);

    LOG_INF("HPM USB init done");
    return true;
}

bool UsbHalHpm::Connect()
{
    // usb_device_init 已内部 Connect，此函数可删除
    // 保留为空调用以兼容现有调用方
    return true;
}

void UsbHalHpm::Disconnect()
{
    usb_device_disconnect(&s_handle);
    ready_ = false;
}

void UsbHalHpm::Deinit()
{
    Disconnect();
}

void UsbHalHpm::InitClockAndPhy()
{
    USB_Type* regs = (USB_Type*)reg_base_;

    clock_add_to_group((clock_name_t)CLOCK_USB0, 0);

    // pinctrl（CherryUSB preinit 中做了）
    // 当前通过 overlay 中的 usb0 节点在 boot 阶段完成引脚配置
    // 如果 bootloader 没配，需要加 pinctrl_apply_state

    // 电源极性（CherryUSB preinit 中做了）
    usb_hcd_set_power_ctrl_polarity(regs, true);

    usb_phy_deinit(regs);
    usb_phy_init(regs, false);

    // PHY 稳定延时（CherryUSB preinit 中做了）
    k_sleep(K_MSEC(100));

    LOG_INF("PHY init done");
}

bool UsbHalHpm::SetAddress(uint8_t address)
{
    // 使用 usb_dcd_set_address（只写 DEVICEADDR，不发 STATUS）
    // UsbDevice 层已处理 STATUS 阶段
    usb_dcd_set_address((USB_Type*)reg_base_, address);
    return true;
}

usb::Speed UsbHalHpm::GetSpeed() const
{
    if (!ready_) return usb::Speed::Full;
    USB_Type* regs = (USB_Type*)reg_base_;
    uint32_t pspd = (regs->PORTSC1 & USB_PORTSC1_PSPD_MASK) >> USB_PORTSC1_PSPD_SHIFT;
    return (pspd == 2) ? usb::Speed::High : usb::Speed::Full;
}

bool UsbHalHpm::EpOpen(const usb::EndpointConfig& cfg)
{
    if (!ready_) return false;

    usb_endpoint_config_t ep {};
    switch (cfg.type) {
    case usb::EndpointType::Control:     ep.xfer = 0; break;
    case usb::EndpointType::Isochronous: ep.xfer = 1; break;
    case usb::EndpointType::Bulk:        ep.xfer = 2; break;
    case usb::EndpointType::Interrupt:   ep.xfer = 3; break;
    }
    ep.ep_addr         = cfg.address;
    ep.max_packet_size = cfg.max_packet_size;

    // 使用 usb_device_edpt_open（配置 QHD + ENDPTCTRL）
    // 替代 usb_dcd_edpt_open（只配 ENDPTCTRL，不配 QHD）
    if (!usb_device_edpt_open(&s_handle, &ep)) {
        return false;
    }

    // 更新端点使能状态
    uint8_t idx = cfg.address & 0x0F;
    if (cfg.address & 0x80) {
        s_in_ep[idx].enable = true;
    } else {
        s_out_ep[idx].enable = true;
    }

    return true;
}

bool UsbHalHpm::EpClose(uint8_t ep)
{
    uint8_t idx = ep & 0x0F;
    if (ep & 0x80)
        s_in_ep[idx].enable = false;
    else
        s_out_ep[idx].enable = false;

    usb_dcd_edpt_close((USB_Type*)reg_base_, ep);
    return true;
}

bool UsbHalHpm::EpStall(uint8_t ep, bool stall)
{
    if (stall)
        usb_dcd_edpt_stall((USB_Type*)reg_base_, ep);
    else
        usb_dcd_edpt_clear_stall((USB_Type*)reg_base_, ep);
    return true;
}

bool UsbHalHpm::EpStartRx(uint8_t ep, uint16_t len)
{
    if (!ready_) return false;

    uint8_t idx = ep & 0x0F;
    if (!s_out_ep[idx].enable) return false;

    uint8_t* buf = s_rx_buf[rx_idx_];
    rx_idx_ = (rx_idx_ == 0) ? 1 : 0;

    s_out_ep[idx].buf = buf;
    s_out_ep[idx].len = len;

    // EpStartRx 前 invalidate（防止 CPU 脏 cache 影响 DMA 写入）
    l1c_dc_invalidate((uintptr_t)buf, HPM_L1C_CACHELINE_ALIGN_UP(len));

    return usb_device_edpt_xfer(&s_handle, ep, buf, len);
}

bool UsbHalHpm::EpStartTx(uint8_t ep, const uint8_t* data, uint16_t len)
{
    if (!ready_) return false;

    if (data != nullptr && len > 0) {
        uint8_t idx = ep & 0x0F;
        if (!s_in_ep[idx].enable) return false;

        memcpy(s_tx_buf, data, len);
        s_in_ep[idx].buf = s_tx_buf;
        s_in_ep[idx].len = len;

        l1c_dc_writeback((uintptr_t)s_tx_buf, HPM_L1C_CACHELINE_ALIGN_UP(len));
        return usb_device_edpt_xfer(&s_handle, ep, s_tx_buf, len);
    }
    return usb_device_edpt_xfer(&s_handle, ep, nullptr, 0);
}

bool UsbHalHpm::Ep0StartIn(const uint8_t* data, uint16_t len)
{
    // EP0 DATA IN：所有数据通过 nocache s_tx_buf 发送，消除 cache 一致性问题
    // CherryUSB 的 EP0 缓冲 req_data 也在 USB_NOCACHE_RAM_SECTION（.nocache）
    // 直接 DMA 从 nocache 内存读，不需要 writeback
    if (!ready_ || !s_in_ep[0].enable) {
        LOG_ERR("Ep0StartIn rejected: ready=%d enable=%d len=%u",
                ready_, s_in_ep[0].enable, len);
        return false;
    }

    if (len > sizeof(s_tx_buf)) {
        LOG_ERR("Ep0StartIn too large: len=%u buffer=%u",
                len, (unsigned)sizeof(s_tx_buf));
        return false;
    }

    bool ok;
    if (data != nullptr && len > 0) {
        memcpy(s_tx_buf, data, len);
        if (len >= 8) {
            LOG_INF("EP0 IN data=%02x %02x %02x %02x %02x %02x %02x %02x",
                    s_tx_buf[0], s_tx_buf[1], s_tx_buf[2], s_tx_buf[3],
                    s_tx_buf[4], s_tx_buf[5], s_tx_buf[6], s_tx_buf[7]);
        } else {
            LOG_INF("EP0 IN data_len=%u first=%02x", len, s_tx_buf[0]);
        }
        ok = usb_device_edpt_xfer(&s_handle, 0x80, s_tx_buf, len);
    } else {
        ok = usb_device_edpt_xfer(&s_handle, 0x80, nullptr, 0);
    }

    dcd_qhd_t* qhd = usb_device_qhd_get(&s_handle, 1);
    LOG_INF("EP0 IN submit ok=%d len=%u qhd=%p qtd=%p token=0x%08x "
            "ENDPTCTRL0=0x%08x PRIME=0x%08x",
            ok,
            len,
            qhd,
            qhd->attached_qtd,
            qhd->attached_qtd != nullptr ? qhd->attached_qtd->token : 0,
            s_handle.regs->ENDPTCTRL[0],
            s_handle.regs->ENDPTPRIME);
    if (!ok) LOG_ERR("Ep0StartIn(len=%u) failed", len);
    return ok;
}

bool UsbHalHpm::Ep0StartOut(uint8_t* data, uint16_t len)
{
    // 记录 EP0 OUT buffer，便于完成时传递数据指针
    s_out_ep[0].buf = data;
    s_out_ep[0].len = len;

    // EP0 DATA OUT：DMA 写前 invalidate，清除 CPU 可能残留的脏 cache
    if (data != nullptr && len > 0) {
        l1c_dc_invalidate((uintptr_t)data, HPM_L1C_CACHELINE_ALIGN_UP(len));
    }
    bool ok = usb_device_edpt_xfer(&s_handle, 0x00, data, len);
    if (!ok) LOG_ERR("Ep0StartOut(len=%u) failed", len);
    return ok;
}

bool UsbHalHpm::Ep0StatusIn()
{
    bool ok = usb_device_edpt_xfer(&s_handle, 0x80, nullptr, 0);
    if (!ok) LOG_ERR("Ep0StatusIn failed");
    return ok;
}

bool UsbHalHpm::Ep0StatusOut()
{
    bool ok = usb_device_edpt_xfer(&s_handle, 0x00, nullptr, 0);
    if (!ok) LOG_ERR("Ep0StatusOut failed");
    return ok;
}

void UsbHalHpm::Isr()
{
    USB_Type* regs = s_handle.regs;
    uint32_t sts = regs->USBSTS & regs->USBINTR;
    regs->USBSTS = sts;

    LOG_INF("ISR sts=0x%08x PORTSC1=0x%08x", sts, regs->PORTSC1);

    if (sts & kIntrError)
        LOG_ERR("USB error 0x%08x", sts);

    if (sts & kIntrReset) {
        LOG_INF("ISR: USB RESET");
        HandleReset();
    }

    if (sts & kIntrPortChange) {
        LOG_INF("ISR: PortChange CCS=%d", !!(regs->PORTSC1 & USB_PORTSC1_CCS_MASK));
        Event ev {};
        if (regs->PORTSC1 & USB_PORTSC1_CCS_MASK) {
            ev.type = EventType::Connected;
            ev.speed = GetSpeed();
        } else {
            ev.type = EventType::Disconnected;
        }
        if (callback_) callback_(context_, ev);
    }

    if (sts & kIntrUsb) {
        uint32_t comp = regs->ENDPTCOMPLETE;
        uint32_t setup = regs->ENDPTSETUPSTAT;
        LOG_INF("ISR: UE comp=0x%08x setup=0x%08x", comp, setup);
        if (comp) { regs->ENDPTCOMPLETE = comp; HandleTransferComplete(comp); }
        if (setup) { regs->ENDPTSETUPSTAT = setup; HandleSetupReceived(); }
    }
}

void UsbHalHpm::HandleReset()
{
    LOG_INF("HandleReset: before bus_reset");
    // 清端点状态
    memset(s_in_ep, 0, sizeof(s_in_ep));
    memset(s_out_ep, 0, sizeof(s_out_ep));

    usb_device_bus_reset(&s_handle, 64);

    LOG_INF("HandleReset: after bus_reset, opening EP0");

    // 显式打开 EP0（CherryUSB usbd_event_reset_handler 标准路径）
    // 复位会清 ENDPTCTRL[0]，不重新打开 EP0 无法收发 SETUP/DATA
    usb_endpoint_config_t ep0_cfg {};
    ep0_cfg.xfer            = 0;            // 控制端点
    ep0_cfg.max_packet_size = 64;

    ep0_cfg.ep_addr = 0x00;                 // EP0 OUT
    bool ok = usb_device_edpt_open(&s_handle, &ep0_cfg);
    LOG_INF("HandleReset: EP0 OUT open=%d", ok);
    s_out_ep[0].enable = ok;

    ep0_cfg.ep_addr = 0x80;                 // EP0 IN
    ok = usb_device_edpt_open(&s_handle, &ep0_cfg);
    LOG_INF("HandleReset: EP0 IN open=%d ENDPTCTRL0=0x%08x", ok, s_handle.regs->ENDPTCTRL[0]);
    s_in_ep[0].enable = ok;

    Event ev {}; ev.type = EventType::Reset;
    if (callback_) callback_(context_, ev);
}

void UsbHalHpm::HandleSetupReceived()
{
    dcd_qhd_t* qhd = usb_device_qhd_get(&s_handle, 0);
    memcpy(s_setup_buf, (uint8_t*)&qhd->setup_request, 8);
    LOG_INF("Setup: %02x %02x %04x %04x %04x",
            s_setup_buf[0], s_setup_buf[1],
            (uint16_t)(s_setup_buf[2] | (s_setup_buf[3] << 8)),
            (uint16_t)(s_setup_buf[4] | (s_setup_buf[5] << 8)),
            (uint16_t)(s_setup_buf[6] | (s_setup_buf[7] << 8)));
    Event ev {}; ev.type = EventType::SetupReceived;
    memcpy(ev.setup, s_setup_buf, 8);
    if (callback_) callback_(context_, ev);
}

void UsbHalHpm::HandleTransferComplete(uint32_t comp)
{
    for (uint8_t i = 0; i < USB_SOC_DCD_MAX_ENDPOINT_COUNT * 2; i++) {
        uint32_t bit = (i / 2) + ((i % 2) ? 16 : 0);
        if (!(comp & (1 << bit))) continue;

        uint8_t ep = (i / 2) | ((i & 0x01) ? 0x80 : 0);
        bool qtd_error = false;
        uint32_t len = CalcTransferLength(i, &qtd_error);

        Event ev {};
        ev.type = EventType::TransferComplete;
        ev.endpoint = ep;
        ev.length = (uint16_t)len;
        if (qtd_error) {
            ev.error = true;
            ev.length = 0;
            ev.data = nullptr;
            if (callback_) callback_(context_, ev);
            continue;
        }

        // OUT 端点：传 buffer 指针 + invalidate cache（对齐到 cacheline）
        if ((ep & 0x80) == 0 && len > 0) {
            uint8_t idx = ep & 0x0F;
            if (s_out_ep[idx].buf) {
                ev.data = s_out_ep[idx].buf;
                l1c_dc_invalidate((uintptr_t)ev.data, HPM_L1C_CACHELINE_ALIGN_UP(len));
            }
        }

        if (callback_) callback_(context_, ev);
    }
}

/**
 * @brief 计算 qTD 链传输长度，并检查错误状态
 * @param idx  端点索引
 * @param error  输出：true=发生了 transaction/buffer error
 * @return 传输的字节数（即使 error=true 也返回已完成的字节数）
 */
uint32_t UsbHalHpm::CalcTransferLength(uint8_t idx, bool* error)
{
    dcd_qhd_t* qhd = usb_device_qhd_get(&s_handle, idx);
    dcd_qtd_t* qtd = qhd->attached_qtd;
    uint32_t total = 0;
    bool has_error = false;

    while (qtd && !qtd->active) {
        if (qtd->halted || qtd->xact_err || qtd->buffer_err) {
            LOG_ERR("qTD error: halted=%d xact_err=%d buf_err=%d",
                    qtd->halted, qtd->xact_err, qtd->buffer_err);
            qtd->in_use = false;
            // 继续回收链上剩余 qTD
            while (qtd->next != USB_SOC_DCD_QTD_NEXT_INVALID) {
                qtd = (dcd_qtd_t*)(uintptr_t)qtd->next;
                qtd->in_use = false;
            }
            has_error = true;
            break;
        }
        total += qtd->expected_bytes - qtd->total_bytes;
        qtd->in_use = false;
        if (qtd->next == USB_SOC_DCD_QTD_NEXT_INVALID) break;
        qtd = (dcd_qtd_t*)(uintptr_t)qtd->next;
    }

    if (error) *error = has_error;
    return total;
}
