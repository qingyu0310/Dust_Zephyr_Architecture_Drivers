/**
 * @file ecat_eeprom.hpp
 * @author qingyu
 * @brief SII（EEPROM）钩子安装 — 把 EcatHal 的 EEPROM 接口挂到 SSC
 * @version 0.1
 * @date 2026-10-29
 *
 * MainInit() 会把 4 个 pAPPL_EEPROM_* 钩子清成 NULL，
 * 所以必须在它之后调用 Install()。
 *
 * 只有 Flash 模拟模式才需要应用层接管读写；
 * 用板上 I2C EEPROM 时 ESC 自己走 I2C 主控，钩子保持 NULL。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "interface/ecat_hal.hpp"

/**
 * @brief SII 钩子安装工具
 */
class EcatEeprom
{
public:
	/**
	 * @brief 把 4 个 pAPPL_EEPROM_* 钩子装到 SSC
	 *
	 * @param hal  提供 EEPROM 读写能力的硬件抽象层
	 */
	static void Install(EcatHal& hal);

	/**
	 * @brief 卸载（钩子恢复 NULL）
	 */
	static void Uninstall();
};
