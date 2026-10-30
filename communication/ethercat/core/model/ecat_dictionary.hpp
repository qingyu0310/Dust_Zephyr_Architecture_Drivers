/**
 * @file ecat_dictionary.hpp
 * @author qingyu
 * @brief 对象字典：查找、读写、Complete Access、权限与状态门
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "spec/ecat_application_objects.hpp"
#include "spec/ecat_object.hpp"
#include "ecat_types.hpp"

namespace ecat {

/**
 * @brief 数据来源类别
 *
 * Member 是 ObjectData 的成员、Constant 是只读常量、
 * PDO 映射与分配表是逻辑表（按子索引查表，不按内存读）。
 */
enum class DataKind : uint8_t
{
	Member,		// 数据是 ObjectData 的成员
	Constant,	// 只读常量
	RxPdoMapping,	// 0x1600 映射表
	TxPdoMapping,	// 0x1A00 映射表
	RxPdoAssignment,// 0x1C12 分配表
	TxPdoAssignment,// 0x1C13 分配表
};

/**
 * @brief 对象表的一行
 *
 * data_offset 是相对 ObjectData 起始的字节偏移，用 offsetof 求值。
 */
struct ObjectSlot
{
	uint16_t    index;		// 对象索引
	ObjectCode  code;		// 对象代码
	uint8_t     sub_count;		// 子条目数
	const EntryDescription* entries;// 子条目描述表
	const char* name;		// 名称串
	uint16_t    data_offset;	// 相对 ObjectData 的字节偏移
	DataKind    kind;		// 数据来源类别
	const void* const_data;		// kind=Constant 时的数据地址
	uint32_t (*access_hook)(void* context, uint8_t sub_index, uint32_t data_size, void* data, bool complete_access);	// 访问前门，时间戳这类算出来的对象用
	void* hook_context;		// 前门的上下文
	const uint16_t* member_offsets;	// kind=Member 时各子索引相对 ObjectData 的真实字节偏移（offsetof 求值），下标 = 子索引
};

/**
 * @brief 对象字典
 *
 * 拥有可写对象的数据（ObjectData data_），提供按索引/子索引的访问。
 * 不拥有状态机：当前 AL 状态通过 AlStateQuery 回调取得。
 */
class EcatDictionary final
{
public:
	using AlStateQuery = AlState (*)(void* context);	// 当前 AL 状态的查询回调
	using TimestampSource = uint64_t (*)(void* context);	// 0x10F8 时间戳提供者

	EcatDictionary() = default;

	EcatDictionary(const EcatDictionary&) = delete;
	EcatDictionary& operator=(const EcatDictionary&) = delete;

	void BindStateQuery(AlStateQuery query, void* context);	// 绑定状态查询回调
	void BindTimestamp(TimestampSource source, void* context);	// 绑定 0x10F8 的时间戳来源

	ObjectData& Data() { return data_; }					// 可写对象数据的引用，供 EcatSync 绑定 0x1C32/33
	const ObjectData& Data() const { return data_; }

	const ObjectSlot* Find(uint16_t index) const;			// 按索引找对象，找不到返回 nullptr

	const EntryDescription* EntryDescriptionOf(const ObjectSlot& slot, uint8_t sub_index) const;	// 取子索引的条目描述
	void*	 DataOf(const ObjectSlot& slot, uint8_t sub_index) const;								// 取子条目的数据指针
	uint32_t LengthOf(const ObjectSlot& slot, uint8_t sub_index) const;								// 单个条目的字节长度
	uint32_t TotalLengthOf(const ObjectSlot& slot) const;											// 整对象的字节长度
	uint32_t TotalLengthOf(const ObjectSlot& slot, uint8_t sub_index) const;							// 整对象的字节长度（CA 起始子索引）
	uint16_t SubCountOf(const ObjectSlot& slot) const;												// 实际子条目数

	uint32_t Read(uint16_t index, uint8_t sub_index, bool complete_access, void* data, uint32_t capacity, uint32_t* written);
	uint32_t Write(uint16_t index, uint8_t sub_index, bool complete_access, const void* data, uint32_t size);

	uint16_t CollectIndices(uint8_t list_type, uint16_t* out, uint16_t capacity) const;				// 按列表类型枚举对象索引

	static uint32_t MappingEntryOf(DataKind kind, uint8_t sub_index);								// 取 PDO 映射表的一项
	static uint16_t AssignmentEntryOf(DataKind kind, uint8_t sub_index);							// 取 PDO 分配表的一项

private:
	ObjectData   data_ {};							// 可写对象的数据
	AlStateQuery state_query_   = nullptr;			// 状态查询回调
	void*        state_context_ = nullptr;			// 回调上下文
	TimestampSource timestamp_source_ = nullptr;	// 0x10F8 时间戳来源
	void*        timestamp_context_   = nullptr;	// 时间戳来源上下文
	mutable uint64_t timestamp_value_ = 0;			// 0x10F8 的读写缓冲（DataOf 是 const）
	
	bool AllowedInCurrentState(uint16_t flags, bool write) const;	// 当前状态能否读/写
};

}
