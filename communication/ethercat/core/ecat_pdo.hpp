/**
 * @file ecat_pdo.hpp
 * @author qingyu
 * @brief PDO 映射长度计算 — 按 0x1C12 / 0x1C13 的实际分配算 SM2/SM3 字节数
 * @version 0.1
 * @date 2026-10-29
 *
 * 纯计算，只依赖 SSC 的对象字典。不含任何硬件访问。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "interface/ecat_types.hpp"

/**
 * @brief PDO 映射工具
 */
class EcatPdo
{
public:
	/**
	 * @brief 扫描对象字典算过程数据长度
	 *
	 * 结果同时写进 SSC 的 nPdInputSize / nPdOutputSize（协议栈要用），
	 * 并以 PdoLayout 返回给上层。
	 *
	 * @return 长度；rx_size 与 tx_size 都是 0 表示映射失败
	 */
	static PdoLayout Generate();
};
