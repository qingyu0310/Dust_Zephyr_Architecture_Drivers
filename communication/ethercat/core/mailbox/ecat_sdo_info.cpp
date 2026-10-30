/**
 * @file ecat_sdo_info.cpp
 * @author qingyu
 * @brief SDO Information 服务实现
 * @version 0.2
 * @date 2026-10-31
 *
 * 线上布局照 SSC sdoserv.h：
 *   公共头   InfoHead(2) + FragmentsLeft(2)
 *   列表     ListType(2) + 索引数组(2*n)
 *   对象描述 Index(2) + DataType(2) + ObjFlags(2) + 名称
 *   条目描述 Index(2) + Info(2) + DataType(2) + BitLength(2) + ObjAccess(2) + 名称
 *
 * @copyright Copyright (c) 2026
 */

#include "mailbox/ecat_sdo_info.hpp"

#include <cstring>

#include "model/ecat_dictionary.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "spec/ecat_wire.hpp"

namespace ecat {

namespace {

constexpr uint16_t kCoeOffset 		= kMailboxHeaderSize;						// CoE 头偏移（相对报文起点）
constexpr uint16_t kInfoOffset 		= kMailboxHeaderSize + coe::kHeaderSize;	// SDO Info 公共头偏移
constexpr uint16_t kInfoHeaderSize 	= 4;										// InfoHead 2 + FragmentsLeft 2
constexpr uint16_t kDataOffset 		= kInfoOffset + kInfoHeaderSize;			// 服务数据起点

// SSC 线上列表编号：0=各类数量 1=全部 2=RxPDO 3=TxPDO 4=Backup 5=Settings
constexpr uint8_t  kMaxListType 	= 5;
constexpr uint16_t kListTypeSize 	= 2;										// 列表首片开头的 ListType 字段
constexpr uint16_t kObjectDescriptionFixed = 6;									// Index 2 + DataType 2 + ObjFlags 2
constexpr uint16_t kEntryDescriptionFixed  = 10;								// Index 2 + Info 2 + DataType 2 + BitLength 2 + ObjAccess 2

/**
 * @brief 写邮箱头的长度字段
 * @param frame          报文
 * @param payload_length 载荷字节数（不含头）
 */
void SetMailboxLength(MailboxFrame* frame, uint16_t payload_length)
{
	wire::WriteLe16(frame->data, mailbox::kLengthOffset, payload_length);
}

/**
 * @brief 取名称串长度（到 '\0' 或 0xFF 终止符）
 * @param text   名称串起点
 * @param max_len 可扫描的最大字节数，防止越过名称区
 * @return 长度（不含终止符）
 */
uint16_t NameLength(const char* text, uint16_t max_len)
{
	uint16_t length = 0;

	if (text == nullptr) {
		return 0;
	}

	while (length < max_len && text[length] != '\0' && static_cast<uint8_t>(text[length]) != 0xFFU) {
		++length;
	}
	return length;
}

/**
 * @brief 跳过前 skip 个名称，返回第 skip 个名称的起点
 * @param text 名称串起点
 * @param skip 要跳过的名称个数
 * @return 目标名称起点；遇到 0xFF 终止返回 nullptr
 */
const char* SkipNames(const char* text, uint8_t skip)
{
	const char* cursor = text;

	for (uint8_t i = 0; i < skip && cursor != nullptr; ++i)
	{
		// 一直走到当前名称结束
		while (*cursor != '\0')
		{
			if (static_cast<uint8_t>(*cursor) == 0xFFU) {
				return nullptr;
			}
			++cursor;
		}
		++cursor;

		// 下一个字节就是终止标记，说明没有更多名称
		if (static_cast<uint8_t>(*cursor) == 0xFFU) {
			return nullptr;
		}
	}
	return cursor;
}

}

/**
 * @brief 绑定字典与应答通道
 * @param dictionary 对象字典
 * @param sink       应答提交回调
 * @param context    回调上下文
 */
void EcatSdoInfo::Bind(EcatDictionary& dictionary, ReplySink sink, void* context)
{
	dictionary_ = &dictionary;
	sink_ = sink;
	sink_context_ = context;
}

/**
 * @brief 清空分片状态
 */
void EcatSdoInfo::Reset()
{
	fragments_left_ = 0;
	next_index_ = 0x1000;
	list_type_ = 0;
}

/**
 * @brief 处理一帧 SDO Info 请求
 * @param frame 报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatSdoInfo::Handle(MailboxFrame* frame)
{
	if (frame == nullptr || frame->data == nullptr || dictionary_ == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	const uint16_t payload_length = wire::ReadLe16(frame->data, mailbox::kLengthOffset);
	const uint32_t total = static_cast<uint32_t>(payload_length) + kMailboxHeaderSize;

	// 公共头必须完整
	if (total < kDataOffset || total > frame->length) {
		return mailbox::kErrorInvalidSize;
	}

	// SSC：InfoHead 是 16 位，低 7 位是 opcode
	const uint16_t info_head = wire::ReadLe16(frame->data, kInfoOffset);
	const uint8_t operation = static_cast<uint8_t>(info_head & sdo_info::kOpcodeMask);

	switch (operation)
	{
		case sdo_info::kOpGetOdList:
			return HandleList(0, frame, static_cast<uint16_t>(total));

		case sdo_info::kOpGetObjectDescription:
			// 对象描述请求需要 Index(2) + SubIndex(1) 共 3 字节服务数据
			if (total < static_cast<uint32_t>(kDataOffset + 3)) {
				return mailbox::kErrorSizeTooShort;
			}
			return HandleObjectDescription(frame);

		case sdo_info::kOpGetEntryDescription:
			if (total < static_cast<uint32_t>(kDataOffset + 3)) {
				return mailbox::kErrorSizeTooShort;
			}
			return HandleEntryDescription(frame);

		default:
			return mailbox::kErrorServiceNotSupported;
	}
}

/**
 * @brief 处理对象列表请求
 * @param list_type 未使用（实际从报文里读）
 * @param frame     报文
 * @param size      请求报文长度
 * @return 0=成功；非 0 为邮箱错误码
 */
uint8_t EcatSdoInfo::HandleList(uint8_t list_type, MailboxFrame* frame, uint16_t size)
{
	(void)list_type;

	if (size < static_cast<uint16_t>(kDataOffset + kListTypeSize)) {
		return mailbox::kErrorSizeTooShort;
	}

	const uint16_t requested = wire::ReadLe16(frame->data, kDataOffset);

	if (requested > kMaxListType) {
		return mailbox::kErrorServiceNotSupported;
	}

	list_type_ = static_cast<uint8_t>(requested);
	next_index_ = 0x1000;
	fragments_left_ = 0;

	// listType 0：返回各列表类型的对象数量（1~5 共五类）
	if (list_type_ == 0) {
		uint8_t* payload = frame->data + kInfoOffset;

		wire::WriteLe16(payload, 0, sdo_info::kOpGetOdListResponse);
		wire::WriteLe16(payload, 2, 0);
		wire::WriteLe16(payload, kInfoHeaderSize, 0);

		for (uint8_t i = 0; i < kMaxListType; ++i) {
			uint16_t indices[32] = {};
			const uint16_t count = dictionary_->CollectIndices(static_cast<uint8_t>(i + 1), indices, 32);

			wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + kListTypeSize + i * 2), count);
		}

		const uint16_t response_length = static_cast<uint16_t>(kDataOffset + kListTypeSize + kMaxListType * 2);

		SetMailboxLength(frame, static_cast<uint16_t>(response_length - kMailboxHeaderSize));
		frame->length = response_length;

		if (sink_ != nullptr) {
			return sink_(sink_context_, frame->data, response_length);
		}
		return 0;
	}

	// 首片可放多少个索引：邮箱载荷减去公共头、CoE 头、ListType
	const uint16_t capacity = static_cast<uint16_t>(kMaxMailboxSize - kMailboxHeaderSize - coe::kHeaderSize - kInfoHeaderSize - kListTypeSize);

	uint16_t indices[32] = {};
	const uint16_t count = dictionary_->CollectIndices(list_type_, indices, 32);
	const uint16_t total_bytes = static_cast<uint16_t>(count * 2);

	if (total_bytes > capacity) {
		const uint16_t fragment_size = static_cast<uint16_t>(capacity + 2);

		fragments_left_ = static_cast<uint16_t>((total_bytes - capacity + (fragment_size - 1)) / fragment_size);
	}

	return HandleListFragment(frame, list_type_, indices, count, total_bytes > capacity);
}

/**
 * @brief 生成下一帧分片应答
 * @param frame 可写的报文缓冲
 * @return 0=成功；非 0 为邮箱错误码
 */
uint8_t EcatSdoInfo::Continue(MailboxFrame* frame)
{
	if (frame == nullptr || frame->data == nullptr || fragments_left_ == 0) {
		return mailbox::kErrorServiceNotSupported;
	}

	uint16_t indices[32] = {};
	const uint16_t count = dictionary_->CollectIndices(list_type_, indices, 32);

	--fragments_left_;

	return HandleListFragment(frame, list_type_, indices, count, fragments_left_ > 0);
}

/**
 * @brief 写一帧列表分片并应答
 * @param frame     报文
 * @param list_type 列表类型
 * @param indices   对象索引数组
 * @param count     数组内有效条数
 * @param more      是否还有后续分片
 * @return 0=成功；非 0 为邮箱错误码
 */
uint8_t EcatSdoInfo::HandleListFragment(MailboxFrame* frame, uint8_t list_type, const uint16_t* indices, uint16_t count, bool more)
{
	uint8_t* payload = frame->data + kInfoOffset;

	// SSC：InfoHead 低 7 位是 opcode，bit7 是 Incomplete 标志
	const uint16_t opcode = static_cast<uint16_t>(sdo_info::kOpGetOdListResponse | (more ? sdo_info::kIncomplete : 0));

	wire::WriteLe16(payload, 0, opcode);
	wire::WriteLe16(payload, 2, fragments_left_);
	wire::WriteLe16(payload, kInfoHeaderSize, list_type);

	// 数据区从 kDataOffset + kListTypeSize 开始，容量按同一基准算，避免写出去却不计入长度
	const uint16_t data_start = static_cast<uint16_t>(kDataOffset + kListTypeSize);
	const uint16_t writable = static_cast<uint16_t>((kMaxMailboxSize - kMailboxHeaderSize - coe::kHeaderSize - kInfoHeaderSize - kListTypeSize) / 2);

	uint16_t written = 0;
	uint16_t cursor = 0;

	// 从 next_index_ 开始，跳过已经发过的
	for (uint16_t i = 0; i < count; ++i)
	{
		if (indices[i] < next_index_) {
			continue;
		}
		cursor = i;
		break;
	}

	for (uint16_t i = cursor; i < count && written < writable; ++i) {
		wire::WriteLe16(frame->data, static_cast<uint16_t>(data_start + written * 2), indices[i]);
		++written;
	}

	if (written > 0) {
		next_index_ = static_cast<uint16_t>(indices[cursor + written - 1] + 1);
	}

	const uint16_t response_length = static_cast<uint16_t>(data_start + written * 2);

	SetMailboxLength(frame, static_cast<uint16_t>(response_length - kMailboxHeaderSize));
	frame->length = response_length;

	if (sink_ != nullptr) {
		return sink_(sink_context_, frame->data, response_length);
	}
	return 0;
}

/**
 * @brief 处理对象描述请求
 *
 * 线上布局：Index(2) + DataType(2) + ObjFlags(2) + 名称
 *
 * @param frame 报文
 * @return 0=成功；非 0 为邮箱错误码
 */
uint8_t EcatSdoInfo::HandleObjectDescription(MailboxFrame* frame)
{
	const uint16_t index = wire::ReadLe16(frame->data, kDataOffset);
	const uint8_t sub_index = frame->data[kDataOffset + 2];

	const ObjectSlot* slot = dictionary_->Find(index);

	if (slot == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	uint8_t* payload = frame->data + kInfoOffset;

	wire::WriteLe16(payload, 0, sdo_info::kOpObjectDescriptionResponse);
	wire::WriteLe16(payload, 2, 0);

	// 数据区：Index / DataType / ObjFlags，顺序照 SSC TSDOINFOOBJ
	wire::WriteLe16(frame->data, kDataOffset, index);
	wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + 2), static_cast<uint16_t>(DataType::Unsigned8));
	wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + 4), static_cast<uint16_t>((slot->sub_count & 0x00FFU) | (static_cast<uint16_t>(slot->code) << 8)));

	(void)sub_index;

	const uint16_t name_offset = static_cast<uint16_t>(kDataOffset + kObjectDescriptionFixed);
	const uint16_t name_max = static_cast<uint16_t>(kMaxMailboxSize - kMailboxHeaderSize - coe::kHeaderSize - kInfoHeaderSize - kObjectDescriptionFixed);
	const uint16_t name_length = NameLength(slot->name, name_max);

	if (slot->name != nullptr) {
		std::memcpy(frame->data + name_offset, slot->name, name_length);
	}
	frame->data[name_offset + name_length] = '\0';

	const uint16_t response_length = static_cast<uint16_t>(name_offset + name_length + 1);

	SetMailboxLength(frame, static_cast<uint16_t>(response_length - kMailboxHeaderSize));
	frame->length = response_length;

	if (sink_ != nullptr) {
		return sink_(sink_context_, frame->data, response_length);
	}
	return 0;
}

/**
 * @brief 处理条目描述请求
 *
 * 线上布局：Index(2) + Info(2) + DataType(2) + BitLength(2) + ObjAccess(2) + 名称
 *
 * @param frame 报文
 * @return 0=成功；非 0 为邮箱错误码
 */
uint8_t EcatSdoInfo::HandleEntryDescription(MailboxFrame* frame)
{
	const uint16_t index = wire::ReadLe16(frame->data, kDataOffset);
	const uint8_t sub_index = frame->data[kDataOffset + 2];

	const ObjectSlot* slot = dictionary_->Find(index);

	if (slot == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	// 先验证子索引再取描述，避免越界
	if (slot->code != ObjectCode::Variable && sub_index > dictionary_->SubCountOf(*slot)) {
		return mailbox::kErrorServiceNotSupported;
	}

	const EntryDescription* entry = dictionary_->EntryDescriptionOf(*slot, sub_index);

	if (entry == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	uint8_t* payload = frame->data + kInfoOffset;

	wire::WriteLe16(payload, 0, sdo_info::kOpEntryDescriptionResponse);
	wire::WriteLe16(payload, 2, 0);

	// 数据区：Index / Info(低 8 位子索引) / DataType / BitLength / ObjAccess
	wire::WriteLe16(frame->data, kDataOffset, index);
	wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + 2), sub_index);
	wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + 4), static_cast<uint16_t>(entry->data_type));
	wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + 6), entry->bit_length);
	wire::WriteLe16(frame->data, static_cast<uint16_t>(kDataOffset + 8), entry->access_flags);

	const uint16_t name_offset = static_cast<uint16_t>(kDataOffset + kEntryDescriptionFixed);
	const uint16_t name_max = static_cast<uint16_t>(kMaxMailboxSize - kMailboxHeaderSize - coe::kHeaderSize - kInfoHeaderSize - kEntryDescriptionFixed);

	// 条目名：从对象名称串里按 '\0' 跳过前 sub_index 个
	const char* entry_name = SkipNames(slot->name, sub_index);
	uint16_t entry_name_length = (entry_name != nullptr) ? NameLength(entry_name, name_max) : 0;

	if (entry_name != nullptr && entry_name_length > 0) {
		std::memcpy(frame->data + name_offset, entry_name, entry_name_length);
	}
	frame->data[name_offset + entry_name_length] = '\0';

	const uint16_t response_length = static_cast<uint16_t>(name_offset + entry_name_length + 1);

	SetMailboxLength(frame, static_cast<uint16_t>(response_length - kMailboxHeaderSize));
	frame->length = response_length;

	if (sink_ != nullptr) {
		return sink_(sink_context_, frame->data, response_length);
	}
	return 0;
}

}
