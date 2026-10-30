/**
 * @file ecat_pdo_layout.hpp
 * @author qingyu
 * @brief 过程数据布局：RxPDO / TxPDO 的字节结构
 * @version 0.1
 * @date 2026-10-31
 *
 * 对象字典的项数、条目表、偏移表和容量上限全部从这里推导，改布局只改本文件。
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

namespace ecat {

/**
 * @brief 主站写给从站的过程数据（RxPDO）
 */
struct PdoOutputs
{
	uint32_t counter[8];	// 输出计数器
};

/**
 * @brief 从站回给主站的过程数据（TxPDO）
 */
struct PdoInputs
{
	uint32_t counter[8];	// 输入计数器
};

static_assert(sizeof(PdoOutputs) % sizeof(uint32_t) == 0, "RxPDO 必须按 32 位量对齐");
static_assert(sizeof(PdoInputs) % sizeof(uint32_t) == 0, "TxPDO 必须按 32 位量对齐");

/**
 * @brief TxPDO 字数，由布局结构体推导
 */
constexpr uint8_t kProcessInputWords = sizeof(PdoInputs) / sizeof(uint32_t);

/**
 * @brief RxPDO 字数，由布局结构体推导
 */
constexpr uint8_t kProcessOutputWords = sizeof(PdoOutputs) / sizeof(uint32_t);

}
