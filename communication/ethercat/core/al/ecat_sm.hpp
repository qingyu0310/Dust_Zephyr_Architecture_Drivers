/**
 * @file ecat_sm.hpp
 * @author qingyu
 * @brief 同步管理器通道的读取、使能与禁用
 * @version 0.1
 * @date 2026-10-30
 *
 * 不做成类：这几个操作不拥有任何状态，只是读寄存器、写寄存器、等生效。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "spec/ecat_registers.hpp"
#include "interface/ecat_esc.hpp"

namespace ecat {

/**
 * @brief 一个同步管理器通道的寄存器视图
 */
struct SyncManagerRegisters
{
	uint32_t address_length = 0;	// 0x0800 + 8*channel：地址（低 16）与长度（高 16）
	uint32_t settings = 0;			// 0x0804 + 8*channel：控制/状态/使能/PDI 控制

	uint16_t Address() const { return static_cast<uint16_t>(address_length  & sync_manager_reg::kAddressMask); }									// 通道起始地址
	uint16_t Length()  const { return static_cast<uint16_t>((address_length & sync_manager_reg::kLengthMask) >> sync_manager_reg::kLengthShift); }	// 通道缓冲长度
	bool IsOneBuffer() const { return (settings & sync_manager_reg::kModeMask) == sync_manager_reg::kModeOneBuffer; }								// 是否单缓冲模式
	bool IsEnabled()   const { return (settings & sync_manager_reg::kEnableValue) != 0; }															// 通道是否已使能
};

SyncManagerRegisters ReadSyncManager(EcatEsc& esc, uint8_t channel);				// 读一个同步管理器通道

void EnableSyncManagerChannel(EcatEsc& esc, uint8_t channel);						// 使能通道并忙等到生效

void DisableSyncManagerChannel(EcatEsc& esc, uint8_t channel);						// 禁用通道并忙等到生效

void AcknowledgeSyncManagerArea(EcatEsc& esc, uint16_t address, uint16_t length);	// 承认一次同步管理器事件

}
