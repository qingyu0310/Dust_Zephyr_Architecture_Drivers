/**
 * @file ecat_process_data.cpp
 * @author qingyu
 * @brief 过程数据域实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "pdo/ecat_process_data.hpp"

#include <cstring>

#include "model/ecat_dictionary.hpp"
#include "spec/ecat_object.hpp"
#include "interface/ecat_esc.hpp"

namespace ecat {

namespace {

/**
 * @brief 累加一张 PDO 分配表指向的映射表的总位长度
 *
 * 只解析已知的两种映射表，不在表里的索引直接判失败，不静默跳过
 * （静默跳过会让布局偏小，主站看到的是半截数据）。
 *
 * @param assignment_kind 分配表的类别（0x1C12 或 0x1C13）
 * @param mapping_kind    被分配表的映射表类别（0x1600 或 0x1A00）
 * @param bits            输出：累计位长度
 * @return 0=成功，否则为 AL 状态码
 */
uint16_t SumMappingBits(DataKind assignment_kind, DataKind mapping_kind, uint32_t* bits)
{
	*bits = 0;

	const uint16_t assigned = EcatDictionary::AssignmentEntryOf(assignment_kind, 1);

	if (assigned != index::kRxPdoMapping && assigned != index::kTxPdoMapping) {
		return al_status::kInvalidOutputMapping;
	}

	const uint8_t words = (mapping_kind == DataKind::RxPdoMapping) ? kProcessOutputWords : kProcessInputWords;

	for (uint8_t sub = 1; sub <= words; ++sub) {
		const uint32_t mapping = EcatDictionary::MappingEntryOf(mapping_kind, sub);

		if (mapping == 0) {
			return al_status::kInvalidOutputMapping;
		}
		*bits += pdo_mapping::BitLengthOf(mapping);
	}
	return al_status::kNoError;
}

}

/**
 * @brief 解析 0x1C12/0x1C13 与 0x1600/0x1A00，算出两方向字节数
 *
 * 失败时不改动现有布局，保证"提交失败不更新一半"。
 *
 * @param dictionary 对象字典
 * @return 0=成功，否则为 AL 状态码
 */
uint16_t EcatProcessData::Configure(const EcatDictionary& dictionary)
{
	(void)dictionary;

	uint32_t output_bits = 0;
	uint32_t input_bits = 0;

	uint16_t result = SumMappingBits(DataKind::RxPdoAssignment, DataKind::RxPdoMapping, &output_bits);

	if (result != al_status::kNoError) {
		return result;
	}

	// 分配表内容必须是 0x1C12 → 0x1600 这一对，否则算出来的长度没有意义
	if (EcatDictionary::AssignmentEntryOf(DataKind::RxPdoAssignment, 1) != index::kRxPdoMapping) {
		return al_status::kInvalidOutputMapping;
	}

	result = SumMappingBits(DataKind::TxPdoAssignment, DataKind::TxPdoMapping, &input_bits);

	if (result != al_status::kNoError) {
		return result;
	}
	if (EcatDictionary::AssignmentEntryOf(DataKind::TxPdoAssignment, 1) != index::kTxPdoMapping) {
		return al_status::kInvalidInputMapping;
	}

	const uint16_t output_bytes = BitsToBytes(output_bits);
	const uint16_t input_bytes = BitsToBytes(input_bits);

	// 容量校验，超额时不动现有布局
	if (output_bytes > kMaxProcessOutputSize) {
		return al_status::kInvalidOutputMapping;
	}
	if (input_bytes > kMaxProcessInputSize) {
		return al_status::kInvalidInputMapping;
	}

	layout_.rx_size = output_bytes;
	layout_.tx_size = input_bytes;
	return al_status::kNoError;
}

/**
 * @brief 校验 SM2/SM3 与邮箱区域是否重叠
 *
 * 缓冲数保守按 3 倍算：实际缓冲模式要到状态机里读 SM 寄存器才知道，
 * 按 3 倍判重叠只会更严格，不会漏判。
 *
 * @param sm2_area     SM2 起始地址
 * @param sm3_area     SM3 起始地址
 * @param mbx_out_addr 邮箱输出区起始
 * @param mbx_out_size 邮箱输出区长度
 * @param mbx_in_addr  邮箱输入区起始
 * @param mbx_in_size  邮箱输入区长度
 * @return 0=无重叠，否则为 AL 状态码
 */
uint16_t EcatProcessData::ValidateAreas(uint16_t sm2_area, uint16_t sm3_area, uint16_t mbx_out_addr, uint16_t mbx_out_size, uint16_t mbx_in_addr, uint16_t mbx_in_size) const
{
	constexpr uint16_t kThreeBufferCount = 3;

	const uint16_t input_span = static_cast<uint16_t>(layout_.tx_size * kThreeBufferCount);
	const uint16_t output_span = static_cast<uint16_t>(layout_.rx_size * kThreeBufferCount);

	// 输入区不得压到邮箱区
	if (((sm3_area + input_span) > mbx_out_addr) && (sm3_area < (mbx_out_addr + mbx_out_size))) {
		return al_status::kInvalidSmInCfg;
	}
	if (((sm3_area + input_span) > mbx_in_addr) && (sm3_area < (mbx_in_addr + mbx_in_size))) {
		return al_status::kInvalidSmInCfg;
	}

	// 输出区不得压到邮箱区，也不得压到输入区
	if (((sm2_area + output_span) > mbx_out_addr) && (sm2_area < (mbx_out_addr + mbx_out_size))) {
		return al_status::kInvalidSmOutCfg;
	}
	if (((sm2_area + output_span) > mbx_in_addr) && (sm2_area < (mbx_in_addr + mbx_in_size))) {
		return al_status::kInvalidSmOutCfg;
	}
	if (((sm2_area + output_span) > sm3_area) && (sm2_area < (sm3_area + layout_.tx_size))) {
		return al_status::kInvalidSmOutCfg;
	}

	return al_status::kNoError;
}

/**
 * @brief ISR 上下文：从 SM2 取回本周期输出并交给业务
 *
 * 顺序与 C 版 PDO_OutputMapping 一致：先读 ESC，再调业务。
 *
 * @param esc     ESC 硬件
 * @param address SM2 起始地址
 */
void EcatProcessData::ConsumeOutputs(EcatEsc& esc, uint16_t address)
{
	if (layout_.rx_size == 0) {
		return;
	}

	esc.Read(address, output_buffer_, layout_.rx_size);

	if (output_sink_ != nullptr) {
		output_sink_(output_context_, output_buffer_, layout_.rx_size);
	}
}

/**
 * @brief ISR 上下文：把业务填好的输入写进 SM3
 *
 * 顺序与 C 版 PDO_InputMapping 一致：先调业务，再写 ESC。
 *
 * @param esc     ESC 硬件
 * @param address SM3 起始地址
 */
void EcatProcessData::PublishInputs(EcatEsc& esc, uint16_t address)
{
	if (layout_.tx_size == 0) {
		return;
	}

	if (input_source_ != nullptr) {
		input_source_(input_context_, input_buffer_, layout_.tx_size);
	}

	esc.Write(address, input_buffer_, layout_.tx_size);
}

/**
 * @brief 业务输出接收者
 * @param handler 回调
 * @param context 上下文
 */
void EcatProcessData::SetOutputSink(OutputSink handler, void* context)
{
	output_sink_ = handler;
	output_context_ = context;
}

/**
 * @brief 业务输入提供者
 * @param handler 回调
 * @param context 上下文
 */
void EcatProcessData::SetInputSource(InputSource provider, void* context)
{
	input_source_ = provider;
	input_context_ = context;
}

/**
 * @brief 清空两方向缓冲
 */
void EcatProcessData::Clear()
{
	std::memset(output_buffer_, 0, sizeof(output_buffer_));
	std::memset(input_buffer_, 0, sizeof(input_buffer_));
}

}
