/**
 * @file phy_jl1111.hpp
 * @author qingyu
 * @brief JL1111 100BASE-TX PHY 驱动（MII/RMII）
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
 * @brief JL1111 PHY 驱动
 *
 * 配置顺序：软复位 → LED 模式 → 接口模式 → ANAR 通告 → 启协商 → 关广播定址。
 * 复位后两颗 PHY 都开着广播响应，所以任意 MDIO 地址都会读到同一个 ID；
 * 地址非 0 的 PHY 必须在配置末尾把广播关掉并写入本机地址。
 */
class PhyJl1111 final : public EcatPhy
{
public:
	PhyJl1111() = default;

	const char* Name() const override { return "JL1111"; }

	uint8_t Address() const override { return cfg_.mdio_address; }

	bool Init(EcatEsc& esc, const PhyConfig& cfg) override;

	bool LinkUp(EcatEsc& esc, bool& up) override;

private:
	PhyConfig   cfg_;
	EcatPhyMdio mdio_;
	bool        configured_ = false;
};
