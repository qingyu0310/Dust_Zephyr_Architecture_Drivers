/**
 * @file ecat_phy_common.hpp
 * @author qingyu
 * @brief Clause-22（IEEE 802.3 MII 管理接口）标准寄存器与公共访问工具
 * @version 0.2
 * @date 2026-10-31
 *
 * 这里只放所有标准 PHY 都有的东西。芯片私有的页寄存器
 * 由各自的 phy_xxx.cpp 自己定义，不进这个文件。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

class EcatEsc;

/**
 * @brief 标准寄存器地址（clause 22）
 */
namespace ecat_phy_reg {	

constexpr uint8_t kBmcr   = 0U;         // Basic Mode Control
constexpr uint8_t kBmsr   = 1U;         // Basic Mode Status
constexpr uint8_t kPhyId1 = 2U;         // PHY Identifier 1（OUI 高 16 位）
constexpr uint8_t kPhyId2 = 3U;         // PHY Identifier 2（OUI 低 6 位 + 型号 + 版本）
constexpr uint8_t kAnar   = 4U;         // Auto-Negotiation Advertisement
constexpr uint8_t kAnlpar = 5U;         // Auto-Negotiation Link Partner Ability

// MDIO 读失败时总线上读到的值
constexpr uint16_t kInvalid = 0xFFFFU;

}

/**
 * @brief BMCR（Basic Mode Control）位
 */
namespace ecat_phy_bmcr {

constexpr uint16_t kReset      = 1U << 15;  // 软复位，自清零
constexpr uint16_t kLoopback   = 1U << 14;  // 环回
constexpr uint16_t kSpeedLsb   = 1U << 13;  // 速率低位（与 bit6 组合）
constexpr uint16_t kAnEnable   = 1U << 12;  // 自协商使能
constexpr uint16_t kPowerDown  = 1U << 11;  // 掉电
constexpr uint16_t kIsolate    = 1U << 10;  // 隔离 MII
constexpr uint16_t kAnRestart  = 1U << 9;   // 重启自协商
constexpr uint16_t kDuplex     = 1U << 8;   // 1=全双工
constexpr uint16_t kSpeedMsb   = 1U << 6;   // 速率高位（与 bit13 组合）

}

/**
 * @brief BMSR（Basic Mode Status）位
 */
namespace ecat_phy_bmsr {

constexpr uint16_t kLink    = 1U << 2;      // 链路状态（latch-low）
constexpr uint16_t kAnDone  = 1U << 5;      // 自协商完成
constexpr uint16_t kExtCap  = 1U << 0;      // 支持扩展寄存器

}

/**
 * @brief ANAR（Auto-Negotiation Advertisement）位
 */
namespace ecat_phy_anar {

constexpr uint16_t k100Full   = 1U << 8;    // 100BASE-TX 全双工
constexpr uint16_t k100Half   = 1U << 7;    // 100BASE-TX 半双工
constexpr uint16_t k10Full    = 1U << 6;    // 10BASE-T 全双工
constexpr uint16_t k10Half    = 1U << 5;    // 10BASE-T 半双工
constexpr uint16_t kSelector  = 1U << 0;    // 802.3 CSMA/CD

}

/**
 * @brief 公共 MDIO 访问工具
 *
 * 所有 PHY 驱动都用这一套，避免每颗芯片各写一遍读/写/改/复位。
 */
class EcatPhyMdio
{
public:
	EcatPhyMdio() = default;

	/**
	 * @param esc  ESC 硬件，提供 MDIO 通道
	 * @param addr 本 PHY 在 ESC MDIO 上的地址
	 */
	EcatPhyMdio(EcatEsc& esc, uint8_t addr) { Bind(esc, addr); }

	/**
	 * @brief 读寄存器
	 */
	bool Read(uint8_t reg, uint16_t* value) const;

	/**
	 * @brief 写寄存器
	 */
	bool Write(uint8_t reg, uint16_t value) const;

	/**
	 * @brief 读-改-写
	 * @param clear 要清零的位
	 * @param set   要置位的位
	 */
	bool Modify(uint8_t reg, uint16_t clear, uint16_t set) const;

	/**
	 * @brief 软复位并等待 BMCR bit15 自清零
	 * @param name  芯片名（日志用）
	 * @return true=复位完成
	 */
	bool SoftReset(const char* name) const;

	/**
	 * @brief 读 PHY ID（PHYID1 高 16 位 | PHYID2 低 16 位）
	 */
	bool ReadId(uint16_t* id1, uint16_t* id2) const
	{
		return Read(ecat_phy_reg::kPhyId1, id1) && Read(ecat_phy_reg::kPhyId2, id2) && *id1 != ecat_phy_reg::kInvalid;
	}

	uint8_t Address() const { return addr_; }

	/**
	 * @brief 绑定 ESC 与 MDIO 地址
	 * @param esc  ESC 硬件
	 * @param addr 本 PHY 在 ESC MDIO 上的地址
	 */
	void Bind(EcatEsc& esc, uint8_t addr)
	{
		esc_ = &esc;
		addr_ = addr;
	}

private:
	EcatEsc* esc_ = nullptr;	// ESC 硬件，Bind 后有效
	uint8_t  addr_ = 0;			// PHY 在 MDIO 上的地址
};
