/**
 * @file phy_tja1101b.hpp
 * @author qingyu
 * @brief TJA1101B 100BASE-T1 PHY 驱动
 * @version 0.3
 * @date 2026-10-31
 *
 * 由业务线程（trd）按板卡组装：实例化本类、填 PhyConfig、Init(esc, cfg)。
 * MDIO 通道来自 EcatEsc 契约，不读 dts、不依赖厂商头。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "ecat_phy.hpp"
#include "ecat_phy_common.hpp"

/**
 * @brief TJA1101B PHY 驱动
 *
 * 100BASE-T1（IEEE 802.3bw）链路固定 100M 全双工，
 * 复位后自动与对端协商 master/slave，主机接口默认 MII。
 * 扩展寄存器（MII/RMII 模式、主从强制、线缆测试）经 REGCR(0x0D)/ADDAR(0x0E)
 * 间接访问，此处保持复位默认值。
 */
class PhyTja1101b final : public EcatPhy
{
public:
	PhyTja1101b() = default;

	const char* Name() const override { return "TJA1101B"; }

	uint8_t Address() const override { return cfg_.mdio_address; }

	bool Init(EcatEsc& esc, const PhyConfig& cfg) override;

	bool LinkUp(EcatEsc& esc, bool& up) override;

private:
	PhyConfig   cfg_;
	EcatPhyMdio mdio_;
	bool        configured_ = false;
};
