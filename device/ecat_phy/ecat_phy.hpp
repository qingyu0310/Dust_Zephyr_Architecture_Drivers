/**
 * @file ecat_phy.hpp
 * @author qingyu
 * @brief EtherCAT PHY 设备抽象
 * @version 0.4
 * @date 2026-10-31
 *
 * PHY 是具体器件模块，与 gpio/pwm 同层。由业务线程（trd）组装，
 * 通过 EcatEsc 契约的 MDIO 方法访问寄存器，不认识板卡也不读设备树。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "interface/ecat_esc.hpp"
#include "interface/ecat_types.hpp"

/**
 * @brief PHY 配置，全部来自业务线程的组装代码
 */
struct PhyConfig
{
	uint8_t      mdio_address = 0;							// ESC MDIO 上的地址
	uint16_t     advertise    = 0x0100;						// clause22 ANAR 通告位（bit8=100M FD）
	bool         autoneg      = true;						// 是否开自协商
	EcatPhySpeed speed        = EcatPhySpeed::Mbps100;		// 不自协商时的固定速率
	bool         full_duplex  = true;						// 不自协商时是否全双工
	uint8_t      interface    = 0;							// 0=MII 1=RMII 2=RGMII
	uint8_t      led_mode     = 0;							// 芯片私有 LED 模式（0=驱动默认）
};

/**
 * @brief EtherCAT PHY 设备接口
 */
class EcatPhy
{
public:
	virtual ~EcatPhy() = default;

	virtual const char* Name() const = 0;		// 芯片名（日志用）
	virtual uint8_t  Address() const = 0;		// 本 PHY 在 ESC MDIO 总线上的地址

	/**
	 * @brief 通过 ESC 的 MDIO 通道配置芯片
	 *
	 * 复位 → 接口模式 → 自协商能力 → 启协商。
	 *
	 * @param esc ESC 硬件，提供 MDIO 通道
	 * @param cfg 芯片配置
	 * @return true=成功
	 */
	virtual bool Init(EcatEsc& esc, const PhyConfig& cfg) = 0;

	/**
	 * @brief 通过 ESC 的 MDIO 通道读链路状态
	 * @param esc ESC 硬件，提供 MDIO 通道
	 * @param up  输出：链路是否 up
	 * @return true=读取成功
	 */
	virtual bool LinkUp(EcatEsc& esc, bool& up) = 0;
};
