/**
 * @file ecat_sii_service.hpp
 * @author qingyu
 * @brief SII（EEPROM）命令处理：读、写、重载、延迟落盘
 * @version 0.2
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include "interface/ecat_esc.hpp"

namespace ecat {

/**
 * @brief SII 命令服务
 *
 * 拥有延迟落盘计数与读宽度；介质与寄存器都通过 EcatEsc& 访问。
 */
class EcatSiiService final
{
public:
	EcatSiiService() = default;

	EcatSiiService(const EcatSiiService&) = delete;
	EcatSiiService& operator=(const EcatSiiService&) = delete;

	void Bind(EcatEsc& esc);							// 绑定硬件能力

	void Init();										// 初始化：读 0x0500 判断读宽度是 4 还是 8 字节

	bool PollCommand();									// 处理一个 SII 命令

	bool Tick();										// 1 ms 上下文：推进延迟落盘

	void Flush();										// 立即落盘

	uint16_t ReadWidth() const { return read_width_; }	// 当前读宽度（4 或 8）

private:
	bool HandleRead(uint32_t address);					// 处理读命令
	bool HandleWrite(uint32_t address);					// 处理写命令
	bool HandleReload(uint32_t address, uint32_t& config);	// 处理重载命令

	void PublishStatus(uint32_t error_bits);			// 把错误位写回配置寄存器

	EcatEsc* esc_ = nullptr;							// ESC 硬件（含 SII 介质）

	uint16_t read_width_    = 4;						// SII 读宽度（4 或 8 字节）
	uint16_t store_counter_ = 0;						// 延迟落盘已等待的毫秒数
	uint16_t store_pending_ = 0;						// 非 0 表示有待落盘的改动
};

}
