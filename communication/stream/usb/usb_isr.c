/**
 * @file usb_isr.c
 * @author qingyu
 * @brief USB ISR 注册包装
 * @version 0.1
 * @date 2026-07-27
 *
 * IRQ_CONNECT 必须放在 C 文件中（.intList section 在 C++ 下会链接失败）。
 * CherryUSB 的 CHERRYUSB_HPM_DEFINE 宏也使用同样的 C 文件 + IRQ_CONNECT 模式。
 *
 * @copyright Copyright (c) 2026
 */

#include <zephyr/irq.h>
#include <zephyr/init.h>
#include <zephyr/devicetree.h>

extern void usb_isr_entry(const void* arg);

static int usb_irq_init(void)
{
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(cherryusb_usb0)), 0, usb_isr_entry, NULL, 0);
    irq_enable(DT_IRQN(DT_NODELABEL(cherryusb_usb0)));
    return 0;
}

SYS_INIT(usb_irq_init, PRE_KERNEL_1, 0);
