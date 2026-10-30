/**
 * @file ecat_sm.cpp
 * @author qingyu
 * @brief 同步管理器通道实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "al/ecat_sm.hpp"

#include "spec/ecat_limits.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"

namespace ecat {

namespace {

/**
 * @brief 通道寄存器基址
 * @param channel 通道号
 * @return 0x0800 + 8 * channel
 */
uint16_t BaseOffset(uint8_t channel)
{
	return static_cast<uint16_t>(reg::kSyncManagerBase + channel * kSyncManagerRegSize);
}

/**
 * @brief 控制/状态寄存器偏移
 *
 * 每个通道 8 字节：地址/长度（0）、控制/状态/使能/PDI（4）。
 *
 * @param channel 通道号
 * @return 控制寄存器偏移
 */
uint16_t ControlOffset(uint8_t channel)
{
	return static_cast<uint16_t>(BaseOffset(channel) + 4);
}

/**
 * @brief 读 32 位寄存器
 * @param esc     ESC 硬件
 * @param address 寄存器偏移
 * @return 寄存器值
 */
uint32_t Read32(EcatEsc& esc, uint16_t address)
{
	uint8_t bytes[4] = {};

	esc.Read(address, bytes, sizeof(bytes));
	return wire::ReadLe32(bytes, 0);
}

/**
 * @brief 只改 PDI 控制字节（0x0807），保留控制/状态/使能字节
 * @param esc     ESC 硬件
 * @param address 控制寄存器偏移（0x0804 + 8*n）
 * @param value   PDI 控制字节
 */
void WritePdiControl(EcatEsc& esc, uint16_t address, uint8_t value)
{
	uint8_t bytes[4] = {};

	esc.Read(address, bytes, sizeof(bytes));

	bytes[3] = value;
	esc.Write(address, bytes, sizeof(bytes));
}


}

/**
 * @brief 读一个同步管理器通道
 * @param esc     ESC 硬件
 * @param channel 通道号（0~15）
 * @return 通道寄存器快照
 */
SyncManagerRegisters ReadSyncManager(EcatEsc& esc, uint8_t channel)
{
	SyncManagerRegisters registers;

	registers.address_length = Read32(esc, BaseOffset(channel));
	registers.settings 		 = Read32(esc, ControlOffset(channel));

	return registers;
}

/**
 * @brief 使能一个同步管理器通道并等待生效
 *
 * 忙等到 PDI 禁用位清零。死循环是刻意的：ESC 不响应就是硬件故障。
 *
 * @param esc     ESC 硬件
 * @param channel 通道号
 */
void EnableSyncManagerChannel(EcatEsc& esc, uint8_t channel)
{
	const uint16_t offset = ControlOffset(channel);

	// 0x0804~0x0806 是主站配置（方向/缓冲模式/使能），PDI 只读；
	// 写整个 dword 会把这些位清掉，所以只改 PDI 控制字节（bit24..31）
	WritePdiControl(esc, offset, 0);

	while ((Read32(esc, offset) & sync_manager_reg::kPdiDisable) != 0)
	{
		// 等硬件
	}
}

/**
 * @brief 禁用一个同步管理器通道并等待生效
 *
 * 忙等到 PDI 禁用位置位。
 *
 * @param esc     ESC 硬件
 * @param channel 通道号
 */
void DisableSyncManagerChannel(EcatEsc& esc, uint8_t channel)
{
	const uint16_t offset = ControlOffset(channel);

	// 只置 PDI 控制字节的禁用位，保留主站配置的控制/使能字节
	WritePdiControl(esc, offset, 0x01);

	while ((Read32(esc, offset) & sync_manager_reg::kPdiDisable) == 0)
	{
		// 等硬件
	}
}

/**
 * @brief 承认一次同步管理器事件
 *
 * ESC 要求把该区域的首尾各读一次才算已消费，长度不足 8 时不读末地址。
 *
 * @param esc     ESC 硬件
 * @param address 区域起始地址
 * @param length  区域长度（字节）
 */
void AcknowledgeSyncManagerArea(EcatEsc& esc, uint16_t address, uint16_t length)
{
	uint8_t scratch[4] = {};

	esc.Read(address, scratch, sizeof(scratch));

	if (length >= sizeof(scratch) * 2U) {
		esc.Read(static_cast<uint16_t>(address + length - sizeof(scratch)), scratch, sizeof(scratch));
	}
}

}
