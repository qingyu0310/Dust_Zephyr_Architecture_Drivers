/**
 * @file ecat_dictionary.cpp
 * @author qingyu
 * @brief 对象字典实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "model/ecat_dictionary.hpp"

#include <cstring>

#include "spec/ecat_limits.hpp"
#include "spec/ecat_wire.hpp"

namespace ecat {

namespace {

/**
 * @brief 对象表
 *
 * 19 项索引互不相同：标准对象 11 项 + 应用对象 8 项。
 * data_offset 用 offsetof 求值，整张表 constexpr，落在 .rodata。
 */
/*
 * 各成员对象的子索引真实偏移表（offsetof 求值，下标 = 子索引）
 *
 * 协议位长累加出来的偏移不等于 C++ 结构体的内存偏移：uint16_t 后面接
 * uint32_t 时有 2 字节对齐空隙。必须逐成员用 offsetof，不能按描述表位长累加。
 */
constexpr uint16_t kOffsets0x1018[] = {
	offsetof(ObjectData, identity_sub_count),
	offsetof(ObjectData, vendor_id),
	offsetof(ObjectData, product_code),
	offsetof(ObjectData, revision),
	offsetof(ObjectData, serial_number),
};

constexpr uint16_t kOffsets0x10F1[] = {
	offsetof(ObjectData, error_settings_sub_count),
	offsetof(ObjectData, local_error_reaction),
	offsetof(ObjectData, sync_error_counter_limit),
};

// sync_manager_types 是 uint16_t[2]，但描述表声明每个子项是 1 字节，
// 所以四个子项按字节依次落在数组的 0/1/2/3 偏移上
constexpr uint16_t kOffsets0x1C00[] = {
	offsetof(ObjectData, sync_manager_type_count),
	offsetof(ObjectData, sync_manager_types) + 0,
	offsetof(ObjectData, sync_manager_types) + 1,
	offsetof(ObjectData, sync_manager_types) + 2,
	offsetof(ObjectData, sync_manager_types) + 3,
};

constexpr uint16_t kOffsets0x1C32[] = {
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sync_type),
	offsetof(ObjectData, sync_manager_out.cycle_time),
	offsetof(ObjectData, sync_manager_out.sub003_reserved),
	offsetof(ObjectData, sync_manager_out.sync_types_supported),
	offsetof(ObjectData, sync_manager_out.min_cycle_time),
	offsetof(ObjectData, sync_manager_out.calc_and_copy_time),
	offsetof(ObjectData, sync_manager_out.sub007_reserved),
	offsetof(ObjectData, sync_manager_out.get_cycle_time),
	offsetof(ObjectData, sync_manager_out.delay_time),
	offsetof(ObjectData, sync_manager_out.sync0_cycle_time),
	offsetof(ObjectData, sync_manager_out.sm_event_missed),
	offsetof(ObjectData, sync_manager_out.cycle_exceeded),
	offsetof(ObjectData, sync_manager_out.shift_too_short),
	offsetof(ObjectData, sync_manager_out.sub014_reserved),
	offsetof(ObjectData, sync_manager_out.sub015_reserved),
	offsetof(ObjectData, sync_manager_out.sub016_reserved),
	offsetof(ObjectData, sync_manager_out.sub017_reserved),
	offsetof(ObjectData, sync_manager_out.sub018_reserved),
	offsetof(ObjectData, sync_manager_out.sub_count),	// 19..31 不存在，占位指向起点
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sub_count),
	offsetof(ObjectData, sync_manager_out.sync_error),
};

constexpr uint16_t kOffsets0x1C33[] = {
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sync_type),
	offsetof(ObjectData, sync_manager_in.cycle_time),
	offsetof(ObjectData, sync_manager_in.sub003_reserved),
	offsetof(ObjectData, sync_manager_in.sync_types_supported),
	offsetof(ObjectData, sync_manager_in.min_cycle_time),
	offsetof(ObjectData, sync_manager_in.calc_and_copy_time),
	offsetof(ObjectData, sync_manager_in.sub007_reserved),
	offsetof(ObjectData, sync_manager_in.get_cycle_time),
	offsetof(ObjectData, sync_manager_in.delay_time),
	offsetof(ObjectData, sync_manager_in.sync0_cycle_time),
	offsetof(ObjectData, sync_manager_in.sm_event_missed),
	offsetof(ObjectData, sync_manager_in.cycle_exceeded),
	offsetof(ObjectData, sync_manager_in.shift_too_short),
	offsetof(ObjectData, sync_manager_in.sub014_reserved),
	offsetof(ObjectData, sync_manager_in.sub015_reserved),
	offsetof(ObjectData, sync_manager_in.sub016_reserved),
	offsetof(ObjectData, sync_manager_in.sub017_reserved),
	offsetof(ObjectData, sync_manager_in.sub018_reserved),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sub_count),
	offsetof(ObjectData, sync_manager_in.sync_error),
};

constexpr uint16_t kOffsets0x6000[] = {
	offsetof(ObjectData, input_counter_count),
	offsetof(ObjectData, input_counter) + 0,
	offsetof(ObjectData, input_counter) + 4,
	offsetof(ObjectData, input_counter) + 8,
	offsetof(ObjectData, input_counter) + 12,
	offsetof(ObjectData, input_counter) + 16,
	offsetof(ObjectData, input_counter) + 20,
	offsetof(ObjectData, input_counter) + 24,
	offsetof(ObjectData, input_counter) + 28,
};

constexpr uint16_t kOffsets0x7010[] = {
	offsetof(ObjectData, output_counter_count),
	offsetof(ObjectData, output_counter) + 0,
	offsetof(ObjectData, output_counter) + 4,
	offsetof(ObjectData, output_counter) + 8,
	offsetof(ObjectData, output_counter) + 12,
	offsetof(ObjectData, output_counter) + 16,
	offsetof(ObjectData, output_counter) + 20,
	offsetof(ObjectData, output_counter) + 24,
	offsetof(ObjectData, output_counter) + 28,
};

constexpr uint16_t kOffsets0xF000[] = {
	offsetof(ObjectData, modular_device_sub_count),
	offsetof(ObjectData, index_distance),
	offsetof(ObjectData, max_modules),
};

constexpr ObjectSlot kSlots[] = {
	// 标准对象
	{index::kDeviceType,      ObjectCode::Variable, 0, schema::kEntry0x1000, schema::kName0x1000,
		offsetof(ObjectData, device_type),    DataKind::Member, nullptr, nullptr, nullptr, nullptr },

	{index::kErrorRegister,   ObjectCode::Variable, 0, schema::kEntry0x1001, schema::kName0x1001,
		offsetof(ObjectData, error_register), DataKind::Member, nullptr, nullptr, nullptr, nullptr },

	// 0x1008 / 0x1009 / 0x100A 的数据是只读字符串，放 const_data，不占 RAM
	{index::kDeviceName,      ObjectCode::Variable, 0, schema::kEntry0x1008, schema::kName0x1008,
	  0, DataKind::Constant, kDeviceName,      nullptr, nullptr, nullptr },

	{index::kHardwareVersion, ObjectCode::Variable, 0, schema::kEntry0x1009, schema::kName0x1009,
	  0, DataKind::Constant, kHardwareVersion, nullptr, nullptr, nullptr },

	{index::kSoftwareVersion, ObjectCode::Variable, 0, schema::kEntry0x100A, schema::kName0x100A,
	  0, DataKind::Constant, kSoftwareVersion, nullptr, nullptr, nullptr },

	{index::kIdentity,        ObjectCode::Record,   4, schema::kEntry0x1018, schema::kName0x1018,
	  offsetof(ObjectData, identity_sub_count),       DataKind::Member,    nullptr, nullptr, nullptr, kOffsets0x1018 },

	{index::kErrorSettings,   ObjectCode::Record,   2, schema::kEntry0x10F1, schema::kName0x10F1,
	  offsetof(ObjectData, error_settings_sub_count), DataKind::Member, nullptr, nullptr, nullptr, kOffsets0x10F1 },

	// 0x10F8 由前门算值，前门在绑定时填
	{index::kTimestamp,       ObjectCode::Variable,  0, schema::kEntry0x10F8, schema::kName0x10F8,
	  0, DataKind::Member, nullptr, nullptr, nullptr, nullptr },

	{index::kSyncManagerType, ObjectCode::Array,    schema::kSyncManagerTypeCount,  schema::kEntry0x1C00, schema::kName0x1C00,
	  offsetof(ObjectData, sync_manager_type_count),  DataKind::Member, nullptr, nullptr, nullptr, kOffsets0x1C00 },

	{index::kSyncManagerOutPara,  ObjectCode::Record, 32, schema::kEntry0x1C3x, schema::kName0x1C32,
	  offsetof(ObjectData, sync_manager_out), DataKind::Member, nullptr, nullptr, nullptr, kOffsets0x1C32 },

	{ index::kSyncManagerInPara, ObjectCode::Record, 32, schema::kEntry0x1C3x, schema::kName0x1C33,
	  offsetof(ObjectData, sync_manager_in),  DataKind::Member, nullptr, nullptr, nullptr, kOffsets0x1C33 },

	// 应用对象
	{index::kRxPdoMapping,  	ObjectCode::Record,   8, 	schema::kEntry0x1600, 	schema::kName0x1600,
	  0, DataKind::RxPdoMapping, nullptr, nullptr, nullptr, nullptr },

	{index::kTxPdoMapping, 		ObjectCode::Record,   8, 	schema::kEntry0x1A00, 	schema::kName0x1A00,
	  0, DataKind::TxPdoMapping, nullptr, nullptr, nullptr, nullptr },

	{index::kRxPdoAssignment, 	ObjectCode::Array,    1, 	schema::kEntry0x1C12, 	schema::kName0x1C12,
	  0, DataKind::RxPdoAssignment, nullptr, nullptr, nullptr, nullptr },

	{index::kTxPdoAssignment, 	ObjectCode::Array,    1, 	schema::kEntry0x1C13, 	schema::kName0x1C13,
	  0, DataKind::TxPdoAssignment, nullptr, nullptr, nullptr, nullptr },

	{index::kMailboxData, 		ObjectCode::Variable, 0, 	schema::kEntry0x2000, 	schema::kName0x2000,
	  offsetof(ObjectData, mailbox_data), 			DataKind::Member, nullptr, nullptr, nullptr, nullptr },

	{index::kInputCounter, 		ObjectCode::Array, 	kProcessDataWords, 	schema::kEntry0x6000, 	schema::kName0x6000,
	  offsetof(ObjectData, input_counter_count), 	DataKind::Member, nullptr, nullptr, nullptr, kOffsets0x6000 },

	{index::kOutputCounter, 	ObjectCode::Array, kProcessDataWords, schema::kEntry0x7010, schema::kName0x7010,
	  offsetof(ObjectData, output_counter_count), 	DataKind::Member, nullptr, nullptr, nullptr, kOffsets0x7010 },

	{index::kModularDeviceProfile, ObjectCode::Record, 2, schema::kEntry0xF000, schema::kName0xF000,
	  offsetof(ObjectData, modular_device_sub_count), DataKind::Member, nullptr, nullptr, nullptr, kOffsets0xF000 },
};

constexpr uint16_t kSlotCount = sizeof(kSlots) / sizeof(kSlots[0]);

static_assert(kSlotCount == kObjectCount, "对象表项数与 kObjectCount 不一致");

constexpr uint8_t kArrayDescriptionCount = 2;	// 数组/记录的条目描述只有两条（子索引 0 + 1..n 共用）

/**
 * @brief 按位偏移取字节偏移
 *
 * SSC 的 OBJ_GetEntryOffset 返回位偏移，调用方再 >>4 转成 UINT16* 步进。
 * 这里统一在字典内部算，调用方拿字节地址。
 *
 * @param code       对象代码
 * @param entries    子条目描述表
 * @param sub_index  子索引
 * @return 相对于对象数据起点的字节偏移
 */

}

/**
 * @brief 绑定状态查询回调
 * @param query   查询函数
 * @param context 上下文
 */
void EcatDictionary::BindStateQuery(AlStateQuery query, void* context)
{
	state_query_ = query;
	state_context_ = context;
}

/**
 * @brief 绑定 0x10F8 的时间戳来源
 * @param source  返回当前时间戳的函数
 * @param context 上下文
 */
void EcatDictionary::BindTimestamp(TimestampSource source, void* context)
{
	timestamp_source_ = source;
	timestamp_context_ = context;
}

/**
 * @brief 按索引找对象
 * @param target 对象索引
 * @return 找到返回槽位指针，否则 nullptr
 */
const ObjectSlot* EcatDictionary::Find(uint16_t target) const
{
	for (uint16_t i = 0; i < kSlotCount; ++i) {
		if (kSlots[i].index == target) {
			return &kSlots[i];
		}
	}
	return nullptr;
}

/**
 * @brief 取某个子索引的条目描述
 * @param slot      对象槽位
 * @param sub_index 子索引
 * @return 描述指针；越界返回 nullptr
 */
const EntryDescription* EcatDictionary::EntryDescriptionOf(const ObjectSlot& slot, uint8_t sub_index) const
{
	switch (slot.code)
	{
		case ObjectCode::Variable:
			// 变量只有一条描述，子索引必须是 0
			return (sub_index == 0) ? &slot.entries[0] : nullptr;

		case ObjectCode::Array:
			// 子索引 0 用第一条，1..n 共用第二条
			return (sub_index == 0) ? &slot.entries[0] : &slot.entries[kArrayDescriptionCount - 1];

		case ObjectCode::Record:
		default:
			if (sub_index > slot.sub_count) {
				return nullptr;
			}
			return &slot.entries[sub_index];
	}
}

/**
 * @brief 取子条目的数据指针
 * @param slot      对象槽位
 * @param sub_index 子索引
 * @return 起始地址；逻辑表或无数据返回 nullptr
 */
void* EcatDictionary::DataOf(const ObjectSlot& slot, uint8_t sub_index) const
{
	switch (slot.kind)
	{
		case DataKind::Constant:
			return const_cast<void*>(slot.const_data);

		case DataKind::RxPdoMapping:
		case DataKind::TxPdoMapping:
		case DataKind::RxPdoAssignment:
		case DataKind::TxPdoAssignment:
			// 逻辑表没有内存地址，走 MappingEntryOf / AssignmentEntryOf
			return nullptr;

		case DataKind::Member:
		default:
			break;
	}

	// 0x10F8 的值是算出来的，不落在 ObjectData 里；读时从时间戳来源取快照
	if (slot.index == index::kTimestamp) {
		timestamp_value_ = (timestamp_source_ != nullptr) ? timestamp_source_(timestamp_context_) : 0;

		return &timestamp_value_;
	}

	uint8_t* base = reinterpret_cast<uint8_t*>(const_cast<ObjectData*>(&data_));

	// 复合对象用 offsetof 建的子项偏移表；标量对象（Variable）直接用 slot.data_offset
	if (slot.member_offsets != nullptr) {
		return base + slot.member_offsets[sub_index];
	}

	if (slot.code == ObjectCode::Variable) {
		return base + slot.data_offset;
	}

	return nullptr;
}

/**
 * @brief 取 PDO 映射表的一项
 * @param kind      数据类别
 * @param sub_index 子索引
 * @return 打包后的映射值；越界返回 0
 */
uint32_t EcatDictionary::MappingEntryOf(DataKind kind, uint8_t sub_index)
{
	if (sub_index == 0 || sub_index > kProcessDataWords) {
		return 0;
	}

	const uint32_t* table = (kind == DataKind::RxPdoMapping) ? schema::kMapping0x1600 : schema::kMapping0x1A00;

	return table[sub_index - 1];
}

/**
 * @brief 取 PDO 分配表的一项
 * @param kind      数据类别
 * @param sub_index 子索引
 * @return PDO 索引；越界返回 0
 */
uint16_t EcatDictionary::AssignmentEntryOf(DataKind kind, uint8_t sub_index)
{
	if (sub_index == 0 || sub_index > 1) {
		return 0;
	}

	const uint16_t* table = (kind == DataKind::RxPdoAssignment) ? schema::kAssignment0x1C12 : schema::kAssignment0x1C13;

	return table[sub_index - 1];
}

/**
 * @brief 单个条目的字节长度
 * @param slot      对象槽位
 * @param sub_index 子索引
 * @return 字节长度；越界返回 0
 */
uint32_t EcatDictionary::LengthOf(const ObjectSlot& slot, uint8_t sub_index) const
{
	const EntryDescription* entry = EntryDescriptionOf(slot, sub_index);

	if (entry == nullptr) {
		return 0;
	}
	return ByteWidthOf(entry->bit_length);
}

/**
 * @brief 整对象的字节长度（Complete Access 用）
 * @param slot 对象槽位
 * @return 字节长度
 */
uint32_t EcatDictionary::TotalLengthOf(const ObjectSlot& slot) const
{
	return TotalLengthOf(slot, 0);
}

/**
 * @brief 整对象的字节长度（Complete Access 用）
 *
 * 照 SSC OBJ_GetObjectLength：数组按 pEntryDesc[1].BitLength * maxSubindex，
 * 记录按各条目累加；sub_index == 0 时再加上子索引 0 的 UINT16。
 *
 * @param slot      对象槽位
 * @param sub_index 起始子索引
 * @return 字节长度
 */
uint32_t EcatDictionary::TotalLengthOf(const ObjectSlot& slot, uint8_t sub_index) const
{
	if (slot.code == ObjectCode::Variable) {
		return 0;
	}

	uint32_t bits = 0;

	if (slot.code == ObjectCode::Array) {
		const EntryDescription* element = EntryDescriptionOf(slot, 1);

		if (element == nullptr) {
			return 0;
		}
		bits = static_cast<uint32_t>(element->bit_length) * SubCountOf(slot);
	} else {
		const uint16_t max_sub = SubCountOf(slot);

		for (uint8_t i = 1; i <= max_sub; ++i) {
			const EntryDescription* entry = EntryDescriptionOf(slot, i);

			if (entry != nullptr) {
				bits += entry->bit_length;
			}
		}
	}

	uint32_t size = BitsToBytes(bits);

	if (sub_index == 0) {
		size += 2;
	}
	return size;
}

/**
 * @brief 该对象的实际子条目数
 * @param slot 对象槽位
 * @return 子条目数
 */
uint16_t EcatDictionary::SubCountOf(const ObjectSlot& slot) const
{
	if (slot.code == ObjectCode::Variable) {
		return 0;
	}

	// PDO 映射/分配表是逻辑表，子索引 0 恒为条目数
	switch (slot.kind)
	{
		case DataKind::RxPdoMapping:
		case DataKind::TxPdoMapping:
			return kProcessDataWords;

		case DataKind::RxPdoAssignment:
		case DataKind::TxPdoAssignment:
			return 1;

		default:
			break;
	}

	// 数组/记录的子索引 0 在对象数据最前面，是 UINT16
	void* first = DataOf(slot, 0);

	if (first == nullptr) {
		return slot.sub_count;
	}

	const uint16_t stored = wire::ReadLe16(static_cast<const uint8_t*>(first), 0);

	return static_cast<uint16_t>(stored & 0x00FFU);
}

/**
 * @brief 当前能否在所处状态下访问
 *
 * SSC 的判据：把条目的读权限位左移一位，与当前 AL 状态的低四位做与。
 * ACCESS_READ=0x01 左移成 0x02（PREOP），AL 状态的 PREOP 也是 0x02。
 *
 * @param flags 条目的访问权限位
 * @return true=允许
 */
bool EcatDictionary::AllowedInCurrentState(uint16_t flags, bool write) const
{
	const AlState state = (state_query_ != nullptr) ? state_query_(state_context_) : AlState::Init;

	// INIT/BOOT 下不允许任何 SDO 访问（SSC 的 nAlStatus 只有 PREOP/SAFEOP/OP 三个位参与比较）
	if (state != AlState::PreOp && state != AlState::SafeOp && state != AlState::Op) {
		return false;
	}

	// SSC OBJ_Read/Write：(ObjAccess << 1) & nAlStatus
	// 读位 0x01/0x02/0x04、写位 0x08/0x10/0x20 压回后，左移一位正好是 0x02/0x04/0x08
	uint16_t mask = 0;

	if (write) {
		mask = static_cast<uint16_t>(flags & (access::kWritePreOp | access::kWriteSafeOp | access::kWriteOp));
		mask = static_cast<uint16_t>(mask >> 3);
	} else {
		mask = static_cast<uint16_t>(flags & (access::kReadPreOp | access::kReadSafeOp | access::kReadOp));
	}

	const uint16_t state_bits = static_cast<uint16_t>(1U << static_cast<uint8_t>(state));

	return (static_cast<uint16_t>(mask << 1) & state_bits) != 0;
}

/**
 * @brief 读一个条目
 * @param target          对象索引
 * @param sub_index       子索引
 * @param complete_access 是否整对象读
 * @param data            目标缓冲
 * @param capacity        目标缓冲容量
 * @param written         输出：实际写入的字节数
 * @return 0=成功，否则为 SDO 中止码
 */
uint32_t EcatDictionary::Read(uint16_t target, uint8_t sub_index, bool complete_access,
                              void* data, uint32_t capacity, uint32_t* written)
{
	const ObjectSlot* slot = Find(target);

	if (slot == nullptr) {
		return sdo::kAbortNonexistentObject;
	}
	if (written != nullptr) {
		*written = 0;
	}
	// 变量不支持整对象访问
	if (complete_access && slot->code == ObjectCode::Variable) {
		return sdo::kAbortUnsupportedAccess;
	}

	if (slot->access_hook != nullptr) {
		const uint32_t result = slot->access_hook(slot->hook_context, sub_index, capacity, data, complete_access);

		if (result == 0 && written != nullptr) {
			*written = complete_access ? TotalLengthOf(*slot) : LengthOf(*slot, sub_index);
		}
		return result;
	}

	if (!complete_access) {
		const uint16_t max_sub = SubCountOf(*slot);

		if (slot->code != ObjectCode::Variable && sub_index > max_sub) {
			return sdo::kAbortSubIndexNotExisting;
		}

		const EntryDescription* entry = EntryDescriptionOf(*slot, sub_index);

		if (entry == nullptr || entry->access_flags == 0) {
			return sdo::kAbortUnsupportedAccess;
		}
		if ((entry->access_flags & access::kRead) == 0) {
			return sdo::kAbortUnsupportedAccess;
		}
		if (!AllowedInCurrentState(entry->access_flags, false)) {
			return sdo::kAbortNotInThisState;
		}

		const uint32_t length = ByteWidthOf(entry->bit_length);

		if (length > capacity) {
			return sdo::kAbortParamLengthTooLong;
		}

		const void* source = DataOf(*slot, sub_index);
		uint8_t* destination = static_cast<uint8_t*>(data);

		// PDO 映射/分配表没有内存地址，按子索引从常量表取值
		if (source == nullptr) {
			switch (slot->kind)
			{
				case DataKind::RxPdoMapping:
				case DataKind::TxPdoMapping:
				{
					if (sub_index == 0) {
						destination[0] = kProcessDataWords;
						*written = 1;
						return 0;
					}
					wire::WriteLe32(destination, 0, MappingEntryOf(slot->kind, sub_index));
					*written = 4;
					return 0;
				}
				case DataKind::RxPdoAssignment:
				case DataKind::TxPdoAssignment:
				{
					if (sub_index == 0) {
						destination[0] = 1;
						*written = 1;
						return 0;
					}
					wire::WriteLe16(destination, 0, AssignmentEntryOf(slot->kind, sub_index));
					*written = 2;
					return 0;
				}
				default:
					return sdo::kAbortUnsupportedAccess;
			}
		}

		// 对象数据按自然对齐存放，显式按小端读写，避免非对齐的大宽度拷贝
		switch (length)
		{
			case 1:
				destination[0] = *static_cast<const uint8_t*>(source);
				break;
			case 2:
				wire::WriteLe16(destination, 0, *static_cast<const uint16_t*>(source));
				break;
			case 4:
				wire::WriteLe32(destination, 0, *static_cast<const uint32_t*>(source));
				break;
			case 8:
				wire::WriteLe64(destination, 0, *static_cast<const uint64_t*>(source));
				break;
			default:
				std::memcpy(destination, source, length);
				break;
		}

		if (written != nullptr) {
			*written = length;
		}
		return 0;
	}

	// Complete Access：从请求的子索引一直读到最大子索引
	// SSC OBJ_Read：for (i = subindex; i <= lastSubindex; i++)，lastSubindex = maxSubindex
	uint32_t offset = 0;
	const uint16_t max_sub = SubCountOf(*slot);

	for (uint8_t i = sub_index; i <= max_sub; ++i)
	{
		// SSC：CA 下数组/记录的子索引 0 固定按 UINT16 传输，不看描述表的位长度
		if (i == 0) {
			if (offset + 2U > capacity) {
				return sdo::kAbortParamLengthTooLong;
			}
			wire::WriteLe16(static_cast<uint8_t*>(data), 0, max_sub);
			offset += 2U;
			continue;
		}

		const EntryDescription* entry = EntryDescriptionOf(*slot, i);

		if (entry == nullptr || entry->access_flags == 0) {
			continue;
		}

		const uint32_t length = ByteWidthOf(entry->bit_length);

		if (offset + length > capacity) {
			return sdo::kAbortParamLengthTooLong;
		}

		uint8_t* destination = static_cast<uint8_t*>(data) + offset;
		const void* source = DataOf(*slot, i);

		// PDO 映射/分配表是逻辑表，DataOf 返回空；CA 下必须按子索引从常量表取值，
		// 不能像单子索引路径那样直接跳过，否则主站读不到 PDO 布局
		if (source == nullptr) {
			switch (slot->kind)
			{
				case DataKind::RxPdoMapping:
				case DataKind::TxPdoMapping:
					wire::WriteLe32(destination, 0, MappingEntryOf(slot->kind, i));
					break;

				case DataKind::RxPdoAssignment:
				case DataKind::TxPdoAssignment:
					wire::WriteLe16(destination, 0, AssignmentEntryOf(slot->kind, i));
					break;

				default:
					continue;
			}

			offset += length;
			continue;
		}

		switch (length)
		{
			case 1:
				destination[0] = *static_cast<const uint8_t*>(source);
				break;
			case 2:
				wire::WriteLe16(destination, 0, *static_cast<const uint16_t*>(source));
				break;
			case 4:
				wire::WriteLe32(destination, 0, *static_cast<const uint32_t*>(source));
				break;
			default:
				std::memcpy(destination, source, length);
				break;
		}
		offset += length;
	}

	if (written != nullptr) {
		*written = offset;
	}
	return 0;
}

/**
 * @brief 写一个条目
 * @param target          对象索引
 * @param sub_index       子索引
 * @param complete_access 是否整对象写
 * @param data            源缓冲
 * @param size            源数据字节数
 * @return 0=成功，否则为 SDO 中止码
 */
uint32_t EcatDictionary::Write(uint16_t target, uint8_t sub_index, bool complete_access,
                               const void* data, uint32_t size)
{
	const ObjectSlot* slot = Find(target);

	if (slot == nullptr) {
		return sdo::kAbortNonexistentObject;
	}
	if (complete_access && slot->code == ObjectCode::Variable) {
		return sdo::kAbortUnsupportedAccess;
	}

	if (!complete_access) {
		const uint16_t max_sub = SubCountOf(*slot);

		if (slot->code != ObjectCode::Variable && sub_index > max_sub) {
			return sdo::kAbortSubIndexNotExisting;
		}

		const EntryDescription* entry = EntryDescriptionOf(*slot, sub_index);

		if (entry == nullptr || entry->access_flags == 0) {
			return sdo::kAbortUnsupportedAccess;
		}
		if ((entry->access_flags & (access::kWritePreOp | access::kWriteSafeOp | access::kWriteOp)) == 0) {
			return sdo::kAbortReadOnly;
		}
		if (!AllowedInCurrentState(entry->access_flags, true)) {
			return sdo::kAbortNotInThisState;
		}

		const uint32_t length = ByteWidthOf(entry->bit_length);

		if (size > length + (length & 1U)) {
			return sdo::kAbortParamLengthTooLong;
		}
		// 短数据必须拒绝：否则下面按 4/8 字节读取会越过请求的有效范围
		if (size < length) {
			return sdo::kAbortParamLengthTooShort;
		}

		void* destination = DataOf(*slot, sub_index);

		if (destination == nullptr) {
			return sdo::kAbortUnsupportedAccess;
		}

		const uint8_t* source = static_cast<const uint8_t*>(data);

		switch (length)
		{
			case 1:
				*static_cast<uint8_t*>(destination) = source[0];
				break;
			case 2:
				*static_cast<uint16_t*>(destination) = wire::ReadLe16(source, 0);
				break;
			case 4:
				*static_cast<uint32_t*>(destination) = wire::ReadLe32(source, 0);
				break;
			case 8:
				*static_cast<uint64_t*>(destination) = wire::ReadLe64(source, 0);
				break;
			default:
				std::memcpy(destination, source, length);
				break;
		}
		return 0;
	}

	// Complete Access 写：从请求的子索引开始依次写入（SSC OBJ_Write 同）
	const uint16_t max_sub = SubCountOf(*slot);
	uint32_t offset = 0;

	for (uint8_t i = sub_index; i <= max_sub; ++i)
	{
		// SSC：CA 下数组/记录的子索引 0 固定按 UINT16 传输，跳过不写
		if (i == 0) {
			if (offset + 2U > size) {
				return sdo::kAbortParamLengthTooShort;
			}
			offset += 2U;
			continue;
		}

		const EntryDescription* entry = EntryDescriptionOf(*slot, i);

		if (entry == nullptr || entry->access_flags == 0) {
			continue;
		}

		const uint32_t length = ByteWidthOf(entry->bit_length);

		if (offset + length > size) {
			return sdo::kAbortParamLengthTooShort;
		}

		void* destination = DataOf(*slot, i);

		if (destination != nullptr && (entry->access_flags & (access::kWritePreOp | access::kWriteSafeOp | access::kWriteOp)) != 0 && AllowedInCurrentState(entry->access_flags, true)) 
		{
			const uint8_t* source = static_cast<const uint8_t*>(data) + offset;

			switch (length)
			{
				case 1:
					*static_cast<uint8_t*>(destination) = source[0];
					break;
				case 2:
					*static_cast<uint16_t*>(destination) = wire::ReadLe16(source, 0);
					break;
				case 4:
					*static_cast<uint32_t*>(destination) = wire::ReadLe32(source, 0);
					break;
				default:
					std::memcpy(destination, source, length);
					break;
			}
		}
		offset += length;
	}
	return 0;
}

/**
 * @brief 按列表类型枚举对象索引（SDO Info 用）
 *
 * SSC 用 0x0020 << listType 作掩码，即 listType=1 → 0x40（TxPDO）、
 * listType=2 → 0x80（RxPDO）。这里保持与 SSC 一致的映射，不改语义。
 *
 * @param list_type 列表类型
 * @param out       输出缓冲
 * @param capacity  可写条数
 * @return 实际写入条数
 */
uint16_t EcatDictionary::CollectIndices(uint8_t list_type, uint16_t* out, uint16_t capacity) const
{
	if (out == nullptr || capacity == 0) {
		return 0;
	}

	const uint16_t mask = static_cast<uint16_t>(0x0020U << list_type);
	uint16_t count = 0;

	for (uint16_t i = 0; i < kSlotCount && count < capacity; ++i) {
		const ObjectSlot& slot = kSlots[i];

		if (slot.index < 0x1000) {
			continue;
		}
		if (list_type == 0) {
			out[count++] = slot.index;
			continue;
		}

		// 该对象至少要有一个子条目的访问位命中掩码
		bool matched = false;

		for (uint8_t sub = 0; sub <= slot.sub_count && !matched; ++sub) {
			const EntryDescription* entry = EntryDescriptionOf(slot, sub);

			if (entry != nullptr && (entry->access_flags & mask) != 0) {
				matched = true;
			}
		}

		if (matched) {
			out[count++] = slot.index;
		}
	}
	return count;
}

}
