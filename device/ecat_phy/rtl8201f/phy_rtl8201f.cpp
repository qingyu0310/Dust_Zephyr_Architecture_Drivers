/**
 * @file phy_rtl8201f.cpp
 * @author qingyu
 * @brief RTL8201F 100BASE-TX PHY 驱动实现
 * @version 0.3
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 */

#include "phy_rtl8201f.hpp"

#include <zephyr/kernel.h>

#include "log.hpp"

namespace {
	// PHY ID（PHYID2 低 4 位是版本号，比较时屏蔽）
	constexpr uint32_t kIdMask = 0xFFFFFFF0U;
	constexpr uint32_t kId     = 0x001CC810U;

	// 私有页寄存器
	constexpr uint8_t kPageSelReg = 31U;
	constexpr uint8_t kPageItf    = 7U;     // 接口模式页
	constexpr uint8_t kRmsrReg    = 16U;    // page7 reg16 bit3: 0=MII 1=RMII
	constexpr uint8_t kPageEee    = 4U;     // EEE 能力页
	constexpr uint8_t kEeeReg     = 16U;    // page4 reg16 bit13/12 = EEE
	constexpr uint8_t kPowerReg   = 24U;    // page0 reg24 bit15 = 掉电省电

	/** 速率档位在 BMCR 中的编码（bit13 / bit6） */
	uint16_t SpeedBits(EcatPhySpeed speed)
	{
		switch (speed) 
		{
			case EcatPhySpeed::Mbps1000:
				return ecat_phy_bmcr::kSpeedLsb | ecat_phy_bmcr::kSpeedMsb;

			case EcatPhySpeed::Mbps100:
				return ecat_phy_bmcr::kSpeedLsb;

			case EcatPhySpeed::Mbps10:
			default:
				return 0U;
		}
	}
}

/**
 * @brief 按 dts 参数配置芯片
 * @param cfg  dts 属性值
 * @return true=成功
 */
bool PhyRtl8201f::Init(EcatEsc& esc, const PhyConfig& cfg)
{
	cfg_ = cfg;
	mdio_.Bind(esc, cfg.mdio_address);

	uint16_t id1 = 0U;
	uint16_t id2 = 0U;
	uint16_t anar = 0U;

	if (!mdio_.ReadId(&id1, &id2)) {
		DUST_LOG_INF("%s@%u: no PHY response\n", Name(), Address());
		return false;
	}
	
	const uint32_t id = (static_cast<uint32_t>(id1) << 16) | id2;
	DUST_LOG_INF("%s@%u: PHY ID=0x%08x\n", Name(), Address(), id);

	if ((id & kIdMask) != kId) {
		DUST_LOG_INF("%s@%u: unexpected ID (expected 0x%08x)\n", Name(), Address(), kId);
		return false;
	}

	if (!mdio_.Write(kPageSelReg, 0U) || !mdio_.SoftReset(Name())) {
		DUST_LOG_INF("%s@%u: software reset failed\n", Name(), Address());
		(void)mdio_.Write(kPageSelReg, 0U);
		return false;
	}

	// 接口模式：page7 reg16 bit3 = 0(MII) / 1(RMII)
	const uint16_t itfBit = (cfg_.interface == 0U) ? 0U : (1U << 3);

	if (!mdio_.Write(kPageSelReg, kPageItf) || !mdio_.Modify(kRmsrReg, (1U << 3), itfBit) ||
	    // 关 EEE：EtherCAT 要求 MII 时钟连续，不能进低功耗
	    !mdio_.Write(kPageSelReg, kPageEee) || !mdio_.Modify(kEeeReg, (1U << 13) | (1U << 12), 0U) || !mdio_.Write(kPageSelReg, 0U) ||
	    // 关链路断开省电，保留其余位
	    !mdio_.Modify(kPowerReg, (1U << 15), 0U) ||
	    // 通告能力：ANAR 只写 dts 给的通告位
	    !mdio_.Write(ecat_phy_reg::kAnar, cfg_.advertise)) 
	{
		DUST_LOG_INF("%s@%u: MII/EEE/AN register sequence failed\n", Name(), Address());
		(void)mdio_.Write(kPageSelReg, 0U);
		return false;
	}

	if (!mdio_.Read(ecat_phy_reg::kAnar, &anar) || anar != cfg_.advertise) {
		DUST_LOG_INF("%s@%u: ANAR readback 0x%04x (wrote 0x%04x), continuing\n", Name(), Address(), anar, cfg_.advertise);
	}

	// BMCR：自协商 or 强制速率/双工
	uint16_t bmcr = 0U;

	if (cfg_.autoneg) 
	{
		bmcr = ecat_phy_bmcr::kAnEnable | ecat_phy_bmcr::kAnRestart;
	}
	else 
	{
		bmcr = SpeedBits(cfg_.speed);
		if (cfg_.full_duplex) {
			bmcr |= ecat_phy_bmcr::kDuplex;
		}
	}

	if (!mdio_.Write(ecat_phy_reg::kBmcr, bmcr)) {
		DUST_LOG_INF("%s@%u: BMCR setup failed\n", Name(), Address());
		return false;
	}

	configured_ = true;
	DUST_LOG_INF("%s@%u: configured (%s, %s)\n", Name(), Address(),
	cfg_.autoneg ? "autoneg" : "forced", (cfg_.interface == 0U) ? "MII" : "RMII");

	return true;
}

/**
 * @brief 读链路状态
 * @param up  输出：链路是否 up
 * @return true=读取成功
 */
bool PhyRtl8201f::LinkUp(EcatEsc& esc, bool& up)
{
	(void)esc;

	uint16_t status = 0U;
	uint16_t control = 0U;

	up = false;
	if (!configured_) {
		return false;
	}

	// BMSR 的 LINK 位是 latch-low，连读两次取当前值
	if (!mdio_.Read(ecat_phy_reg::kBmsr, &status) || !mdio_.Read(ecat_phy_reg::kBmsr, &status) || status == ecat_phy_reg::kInvalid || !mdio_.Read(ecat_phy_reg::kBmcr, &control) || control == ecat_phy_reg::kInvalid) {
		return false;
	}

	// 不要求 BMSR bit5（自协商完成）：直连 EtherCAT 时可能靠并行检测 建立 100M 全双工，自协商永远不完成。SDK 的 rtl8201_get_phy_status() 同样只看 bit2。速率/双工从 BMCR 交叉确认。
	up = (status & ecat_phy_bmsr::kLink) != 0U && (control & (ecat_phy_bmcr::kSpeedLsb | ecat_phy_bmcr::kDuplex)) == (ecat_phy_bmcr::kSpeedLsb | ecat_phy_bmcr::kDuplex);

	return true;
}

