/**
 * @file ecat_types.hpp
 * @author qingyu
 * @brief EtherCAT 协议基础类型，不依赖 SSC、不依赖任何厂商头
 * @version 0.1
 * @date 2026-10-29
 *
 * 只放 EtherCAT 规范（ETG.1000）定义的数据类型。
 * 不放寄存器、不放 SSC 结构体、不放任何 hpm_ 前缀符号。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

/**
 * @brief EtherCAT 应用层状态机（AL State）
 */
enum class EcatState : uint8_t
{
	Init   = 0x01,                          // 初始化
	PreOp  = 0x02,                          // 预运行（邮箱可用）
	SafeOp = 0x04,                          // 安全运行（输入有效）
	Op     = 0x08,                          // 运行（输入输出都有效）
	Boot   = 0x03,                          // 引导（固件升级）
};

/**
 * @brief EtherCAT 中断源
 */
enum class EcatIrq : uint8_t
{
	Pdi   = 0,                              // PDI 中断（主中断）
	Sync0 = 1,                              // 分布式时钟 SYNC0
	Sync1 = 2,                              // 分布式时钟 SYNC1
	Reset = 3,                              // ESC 复位
};

/**
 * @brief PHY 速率
 */
enum class EcatPhySpeed : uint8_t
{
	Mbps10   = 0,                           // 10 Mbit/s
	Mbps100  = 1,                           // 100 Mbit/s
	Mbps1000 = 2,                           // 1000 Mbit/s
};

/**
 * @brief 过程数据布局（SM2 / SM3 字节数）
 */
struct PdoLayout
{
	uint16_t rx_size = 0;                   // 主站→从站（SM2 / RxPDO）
	uint16_t tx_size = 0;                   // 从站→主站（SM3 / TxPDO）
};

/**
 * @brief 端口链路状态
 */
struct EcatPortState
{
	bool link[3] = { false, false, false }; // 各端口链路
};
