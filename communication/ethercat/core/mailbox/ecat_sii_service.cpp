/**
 * @file ecat_sii_service.cpp
 * @author qingyu
 * @brief SII 命令服务实现
 * @version 0.2
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 */

#include "mailbox/ecat_sii_service.hpp"

#include "spec/ecat_registers.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"
#include "log.hpp"
#include "spec/ecat_limits.hpp"

namespace ecat {

namespace {

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
 * @brief 写 32 位寄存器
 * @param esc     ESC 硬件
 * @param address 寄存器偏移
 * @param value   待写入的值
 */
void Write32(EcatEsc& esc, uint16_t address, uint32_t value)
{
	uint8_t bytes[4] = {};

	wire::WriteLe32(bytes, 0, value);
	esc.Write(address, bytes, sizeof(bytes));
}

}

/**
 * @brief 绑定硬件能力
 * @param esc ESC 硬件
 * @param sii SII 介质
 */
void EcatSiiService::Bind(EcatEsc& esc)
{
	esc_ = &esc;
}

/**
 * @brief 初始化：读 0x0500 判断读宽度是 4 还是 8 字节
 */
void EcatSiiService::Init()
{
	if (esc_ == nullptr) {
		return;
	}

	// 0x0502.6 决定读宽度：1 表示支持 8 字节读
	const uint32_t config = Read32(*esc_, reg::kEepromConfig);

	read_width_ = ((config & eeprom_config::kSupportedReadBytes) != 0) ? 8 : 4;

}

/**
 * @brief 处理一个 SII 命令
 * @return true=已处理了一个命令
 */
bool EcatSiiService::PollCommand()
{
	if (esc_ == nullptr) {
		return false;
	}

	// 板载 EEPROM 由 ESC 自己走 I2C 主控，软件不该接管这些命令；
	// 只有 Flash 模拟模式才需要应用层应答
	if (esc_->GetSiiMode() != EcatEsc::SiiMode::Emulated) {
		return false;
	}

	uint32_t config = Read32(*esc_, reg::kEepromConfig);

	// 只有忙位有效时才处理
	if ((config & eeprom_config::kBusyMask) == 0) {
		return false;
	}

	const uint32_t command = config & eeprom_config::kCmdMask;
	const uint32_t address = Read32(*esc_, reg::kEepromAddress);

	// 先清掉非 CRC 的错误位，处理过程中遇到失败再置回
	config &= ~(eeprom_config::kErrorMask & ~eeprom_config::kErrorCrc);

	switch (command)
	{
		case 0:
			// 空闲：没有命令要处理
			break;

		case eeprom_config::kCmdRead:
			if (!HandleRead(address)) {
				config |= eeprom_config::kErrorCmdAck;
			}
			break;

		case eeprom_config::kCmdWrite:
			if (!HandleWrite(address)) {
				config |= eeprom_config::kErrorCmdAck;
			}
			// 收到写命令即启动延迟落盘计时
			store_pending_ = 1;
			break;

		case eeprom_config::kCmdReload:
			HandleReload(address, config);
			break;

		default:
			config |= eeprom_config::kErrorCmdAck;
			break;
	}

	// SSC：写回完整的配置寄存器（含错误位）
	Write32(*esc_, reg::kEepromConfig, config);

	// 任何命令处理完都重置落盘计数
	store_counter_ = 0;
	return true;
}

/**
 * @brief 处理读命令
 * @param address SII 字地址
 */
bool EcatSiiService::HandleRead(uint32_t address)
{
	uint8_t bytes[8] = {};

	if (!esc_->ReadSiiWord(address, bytes, static_cast<uint16_t>(read_width_))) {
		return false;
	}
	esc_->Write(reg::kEepromData, bytes, read_width_);
	return true;
}

/**
 * @brief 处理写命令
 * @param address SII 字地址
 */
bool EcatSiiService::HandleWrite(uint32_t address)
{
	// SSC：EEPROM_WRITE_SIZE = 8 字节
	uint8_t bytes[kSiiWriteSize] = {};

	esc_->Read(reg::kEepromData, bytes, sizeof(bytes));

	return esc_->WriteSiiWord(address, bytes, sizeof(bytes));
}

/**
 * @brief 处理重载命令
 *
 * 8 字节读模式下主站逐段发 reload，每段都要把数据搬进 ESC 的数据窗口再确认；
 * 4 字节读模式整块重载，把站点别名（SII 字 4~5）写回 ESC。
 *
 * @param address SII 字地址
 * @return true=重载完成；false=失败
 */
bool EcatSiiService::HandleReload(uint32_t address, uint32_t& config)
{
	if (read_width_ == 8) {
		// SSC：主站逐段发 reload，循环处理到命令不再是 reload
		do
		{
			if (address > kSiiSizeBytes) {
				config |= eeprom_config::kErrorCmdAck;
				return false;
			}

			uint8_t bytes[8] = {};

			if (!esc_->ReadSiiWord(address, bytes, static_cast<uint16_t>(read_width_))) {
				config |= eeprom_config::kErrorCmdAck;
				return false;
			}
			esc_->Write(reg::kEepromData, bytes, read_width_);

			// 清掉全部错误位后应答当前段
			config &= ~eeprom_config::kErrorMask;
			Write32(*esc_, reg::kEepromConfig, config);

			// 重新读控制寄存器，看主站是否还挂着 reload
			config = Read32(*esc_, reg::kEepromConfig);
			address = Read32(*esc_, reg::kEepromAddress);

		} while ((config & eeprom_config::kCmdMask) == eeprom_config::kCmdReload);

		return true;
	}

	// 4 字节读模式：把站点别名（SII 字 4~5）写回 ESC
	uint8_t alias[4] = {};

	if (!esc_->ReadSiiWord(4, alias, sizeof(alias))) {
		config |= eeprom_config::kErrorCrc;
		return false;
	}
	esc_->Write(reg::kEepromData, alias, sizeof(alias));
	config &= ~eeprom_config::kErrorCrc;
	return true;
}

/**
 * @brief 把错误位写回配置寄存器
 * @param error_bits 要置位的错误掩码
 */
void EcatSiiService::PublishStatus(uint32_t error_bits)
{
	if (esc_ == nullptr) {
		return;
	}

	uint32_t config = Read32(*esc_, reg::kEepromConfig);

	config &= ~(eeprom_config::kErrorMask & ~eeprom_config::kErrorCrc);
	config |= error_bits;
	Write32(*esc_, reg::kEepromConfig, config);
}

/**
 * @brief 1 ms 上下文：推进延迟落盘
 * @return true=本拍执行了落盘
 */
bool EcatSiiService::Tick()
{
	if (store_pending_ == 0) {
		return false;
	}

	++store_counter_;

	if (store_counter_ < kSiiStoreDelayMs) {
		return false;
	}

	esc_->StoreSii();
	store_pending_ = 0;
	store_counter_ = 0;
	return true;
}

/**
 * @brief 立即落盘
 */
void EcatSiiService::Flush()
{
	if (store_pending_ == 0) {
		return;
	}

	esc_->StoreSii();
	store_pending_ = 0;
	store_counter_ = 0;
}

}
