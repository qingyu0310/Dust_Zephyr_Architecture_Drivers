/**
 * @file ecat_eeprom.cpp
 * @author qingyu
 * @brief SII 钩子安装实现
 * @version 0.1
 * @date 2026-10-29
 *
 * @copyright Copyright (c) 2026
 */

#include "ecat_eeprom.hpp"

extern "C" {

#include "applInterface.h"

}

namespace
{
	/** 当前挂着的 HAL（钩子里要用，SSC 的钩子签名不带上下文参数） */
	EcatHal* s_hal = nullptr;

	/**
	 * @brief pAPPL_EEPROM_Read 转发
	 * @param word_addr  SII 字地址
	 * @return 0=成功
	 */
	uint16_t EepromReadHook(uint32_t word_addr)
	{
		if (s_hal == nullptr) {
			return 1U;
		}
		(void)s_hal->EepromRead(word_addr);
		return 0U;
	}

	/**
	 * @brief pAPPL_EEPROM_Write 转发
	 * @param word_addr  SII 字地址
	 * @return 0=成功
	 */
	uint16_t EepromWriteHook(uint32_t word_addr)
	{
		if (s_hal == nullptr) {
			return 1U;
		}
		/* SSC 先把数据写进 ESC 的 EEPROM 数据寄存器，再调本钩子；
		 * HAL 侧只关心「把当前 ESC 里的值落盘」，数据由 HAL 自己读。
		 */
		s_hal->EepromWrite(word_addr, 0U);
		return 0U;
	}

	/**
	 * @brief pAPPL_EEPROM_Reload 转发
	 * @return 0=成功
	 */
	uint16_t EepromReloadHook(void)
	{
		if (s_hal == nullptr) {
			return 1U;
		}
		return static_cast<uint16_t>(s_hal->EepromReload());
	}

	/**
	 * @brief pAPPL_EEPROM_Store 转发
	 */
	void EepromStoreHook(void)
	{
		/* 数据在 Write 钩子里已经落盘，这里无需额外动作 */
	}
}

/**
 * @brief 把 4 个 pAPPL_EEPROM_* 钩子装到 SSC
 * @param hal  提供 EEPROM 读写能力的硬件抽象层
 */
void EcatEeprom::Install(EcatHal& hal)
{
#if defined(CONFIG_HPM_ESC_EEPROM_EMULATION)
	s_hal = &hal;

	pAPPL_EEPROM_Read   = EepromReadHook;
	pAPPL_EEPROM_Write  = EepromWriteHook;
	pAPPL_EEPROM_Reload = EepromReloadHook;
	pAPPL_EEPROM_Store  = EepromStoreHook;
#else
	/* 板上 I2C EEPROM：ESC 自己走 I2C 主控，应用层不接管 */
	(void)hal;
	s_hal = nullptr;
#endif
}

/**
 * @brief 卸载（钩子恢复 NULL）
 */
void EcatEeprom::Uninstall()
{
	s_hal = nullptr;
	pAPPL_EEPROM_Read   = nullptr;
	pAPPL_EEPROM_Write  = nullptr;
	pAPPL_EEPROM_Reload = nullptr;
	pAPPL_EEPROM_Store  = nullptr;
}
