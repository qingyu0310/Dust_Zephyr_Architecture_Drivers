/**
 * @file phy_rtl8201f.hpp
 * @author qingyu
 * @brief RTL8201F 100BASE-TX PHY 驱动（MII/RMII）
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
 * @brief RTL8201F PHY 驱动
 *
 * 配置顺序：软复位 → page7 强制 MII → page4 关 EEE → page0 关省电 → ANAR → 启协商。
 */
class PhyRtl8201f final : public EcatPhy
{
public:
	PhyRtl8201f() = default;

	const char* Name() const override { return "RTL8201F"; }

	uint8_t Address() const override { return cfg_.mdio_address; }

	bool Init(EcatEsc& esc, const PhyConfig& cfg) override;

	bool LinkUp(EcatEsc& esc, bool& up) override;

private:
	PhyConfig   cfg_;
	EcatPhyMdio mdio_;
	bool        configured_ = false;
};
