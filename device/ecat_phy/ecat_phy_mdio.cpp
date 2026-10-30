/**
 * @file ecat_phy_mdio.cpp
 * @author qingyu
 * @brief 公共 MDIO 访问工具实现
 * @version 0.3
 * @date 2026-10-31
 *
 * 三颗 PHY 的软复位流程完全一致（写 BMCR bit15 → 等自清零），
 * 放在这里避免每颗芯片各写一遍。MDIO 通道来自 EcatEsc 契约，
 * 不依赖任何厂商 API。
 *
 * @copyright Copyright (c) 2026
 */

#include <zephyr/kernel.h>

#include "interface/ecat_esc.hpp"
#include "ecat_phy_common.hpp"
#include "log.hpp"

namespace {
	// 复位最长等待（ms），超时按失败处理
	constexpr uint32_t kResetTimeoutMs = 100U;
}

/**
 * @brief 读寄存器
 * @param reg   寄存器地址
 * @param value 输出：读到的值
 * @return true=成功
 */
bool EcatPhyMdio::Read(uint8_t reg, uint16_t* value) const
{
	if (esc_ == nullptr) {
		return false;
	}
	
	return esc_->MdioRead(addr_, reg, value);
}

/**
 * @brief 写寄存器
 * @param reg   寄存器地址
 * @param value 待写入的值
 * @return true=成功
 */
bool EcatPhyMdio::Write(uint8_t reg, uint16_t value) const
{
	if (esc_ == nullptr) {
		return false;
	}

	return esc_->MdioWrite(addr_, reg, value);
}

/**
 * @brief 读-改-写
 * @param reg   寄存器地址
 * @param clear 要清零的位
 * @param set   要置位的位
 * @return true=成功
 */
bool EcatPhyMdio::Modify(uint8_t reg, uint16_t clear, uint16_t set) const
{
	uint16_t value = 0U;

	if (!Read(reg, &value) || value == ecat_phy_reg::kInvalid) {
		return false;
	}

	return Write(reg, static_cast<uint16_t>((value & ~clear) | set));
}

/**
 * @brief 软复位并等待 BMCR bit15 自清零
 * @param name  芯片名（日志用）
 * @return true=复位完成
 */
bool EcatPhyMdio::SoftReset(const char* name) const
{
	uint16_t value = 0;

	if (!Write(ecat_phy_reg::kBmcr, ecat_phy_bmcr::kReset)) {
		return false;
	}

	for (uint32_t elapsed = 0U; elapsed < kResetTimeoutMs; ++elapsed)
	{
		k_msleep(1);
		if (!Read(ecat_phy_reg::kBmcr, &value) || value == ecat_phy_reg::kInvalid) {
			return false;
		}
		if ((value & ecat_phy_bmcr::kReset) == 0U) {
			return true;
		}
	}

	DUST_LOG_INF("%s@%u: software reset timeout (BMCR=0x%04x)\n", name, static_cast<unsigned>(Address()), value);

	return false;
}
