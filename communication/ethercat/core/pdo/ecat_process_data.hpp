/**
 * @file ecat_process_data.hpp
 * @author qingyu
 * @brief 过程数据域：映射解析、布局计算、SM 区校验、两方向缓冲搬运
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "spec/ecat_limits.hpp"
#include "ecat_types.hpp"
#include "interface/ecat_esc.hpp"
#include "interface/ecat_profile.hpp"

namespace ecat {

/**
 * @brief 过程数据域
 *
 * 拥有映射解析结果、两方向字节数与两方向交换缓冲；不拥有状态机，也不决定能否进 OP。
 */
class EcatProcessData final
{
public:
	using OutputSink  = void (*)(void* context, const uint8_t* data, uint16_t length);	// 收到主站数据的回调，业务只读
	using InputSource = void (*)(void* context, uint8_t* data, uint16_t length);		// 提供待发数据的回调，业务写入

	EcatProcessData() = default;

	EcatProcessData(const EcatProcessData&) = delete;
	EcatProcessData& operator=(const EcatProcessData&) = delete;

	void SetProfile(const DeviceProfile& profile) { profile_ = profile; }				// 接收过程数据布局
	uint16_t Configure();																// 按 Profile 与映射表算出两方向字节数

	PdoLayout Layout() const { return layout_; }										// 当前布局

	uint16_t ValidateAreas(uint16_t sm2_area, uint16_t sm3_area, uint16_t mbx_out_addr, uint16_t mbx_out_size, uint16_t mbx_in_addr, uint16_t mbx_in_size) const;	// 校验 SM2/SM3 与邮箱区是否重叠

	void ConsumeOutputs(EcatEsc& esc, uint16_t address);								// ISR 上下文：从 SM2 取回输出交给业务
	void PublishInputs (EcatEsc& esc, uint16_t address);								// ISR 上下文：把业务填好的输入写进 SM3

	void SetOutputSink (OutputSink handler, void* context);								// 业务输出接收者
	void SetInputSource(InputSource provider, void* context);							// 业务输入提供者

	void Clear();	// 清空两方向缓冲，进入 SAFEOP 时清一次

private:
	DeviceProfile profile_ {};															// 业务声明的两方向布局
	PdoLayout layout_ {};																// 两方向字节数

	uint8_t output_buffer_[kMaxProcessBytes] = {};										// 主站→从站
	uint8_t input_buffer_ [kMaxProcessBytes] = {};										// 从站→主站

	OutputSink  output_sink_    = nullptr;												// 输出接收者
	InputSource input_source_   = nullptr;												// 输入提供者
	void*       output_context_ = nullptr;												// 输出回调上下文
	void*       input_context_  = nullptr;												// 输入回调上下文
};

}
