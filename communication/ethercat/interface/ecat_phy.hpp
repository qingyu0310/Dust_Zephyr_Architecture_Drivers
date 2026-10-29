/**
 * @file ecat_phy.hpp
 * @author qingyu
 * @brief EtherCAT PHY 驱动抽象 — 以 PHY 芯片为单位，不依赖任何平台
 * @version 0.1
 * @date 2026-10-29
 *
 * 每颗 PHY 芯片实现一个子类：PhyJl1111 / PhyRtl8201f / PhyTja1101b。
 * 板卡在 dts 里用 compatible 选驱动、用属性填参数，C 代码里没有板卡分支。
 *
 * 职责边界：
 *   - 必须处理：芯片复位、接口模式、自协商、链路读取、私有页寄存器
 *   - 不得处理：ESC 寄存器、中断、EEPROM、AL 状态机、引脚复用
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "ecat_types.hpp"

/**
 * @brief PHY 配置，全部来自 dts 的 PHY 节点属性
 *
 * 驱动只管把这些值翻译成芯片寄存器，不含任何板卡默认值。
 */
struct PhyConfig
{
	uint8_t  mdio_address = 0;             	 		// ESC MDIO 上的地址
	uint16_t advertise    = 0x0100;        	 		// clause22 ANAR 通告位（bit8=100M FD）
	bool     autoneg      = true;           		// 是否开自协商
	EcatPhySpeed speed    = EcatPhySpeed::Mbps100;  // 不自协商时的固定速率
	bool     full_duplex  = true;           		// 不自协商时是否全双工
	uint8_t  interface    = 0;              		// 0=MII 1=RMII 2=RGMII
	uint8_t  led_mode     = 0;              		// 芯片私有 LED 模式（0=驱动默认）
};

/**
 * @brief EtherCAT PHY 驱动接口
 */
class EcatPhy
{
public:
	virtual ~EcatPhy() = default;

	/**
	 * @brief 芯片名（日志用）
	 */
	virtual const char* Name() const = 0;

	/**
	 * @brief 本 PHY 在 ESC MDIO 总线上的地址
	 */
	virtual uint8_t Address() const = 0;

	/**
	 * @brief 按 dts 给的参数配置芯片
	 *
	 * 复位 → 接口模式 → 自协商能力 → 启协商。
	 *
	 * @param cfg  dts 属性值
	 * @return true=成功
	 */
	virtual bool Config(const PhyConfig& cfg) = 0;

	/**
	 * @brief 读链路状态
	 *
	 * @param up  输出：链路是否 up
	 * @return true=读取成功（false 表示 MDIO 失败，调用方按 down 处理）
	 */
	virtual bool LinkUp(bool* up) = 0;
};
