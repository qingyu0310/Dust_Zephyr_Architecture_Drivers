/**
 * @file ecat_object.hpp
 * @author qingyu
 * @brief 对象字典的纯数据定义：类型、权限、条目描述、对象条目、偏移换算
 * @version 0.1
 * @date 2026-10-30
 *
 * 只有数据形状，没有查表逻辑（那在 ecat_dictionary 里）。
 * 数值一律保持 SSC 原值，主站按规范数值访问，改了就不认。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace ecat {

/**
 * @brief 对象字典数据类型（CoE 数据类型，ETG.1000 / CiA 301）
 */
enum class DataType : uint16_t
{
	Boolean        = 0x0001,
	Integer8       = 0x0002,
	Integer16      = 0x0003,
	Integer32      = 0x0004,
	Unsigned8      = 0x0005,
	Unsigned16     = 0x0006,
	Unsigned32     = 0x0007,
	Real32         = 0x0008,
	VisibleString  = 0x0009,
	OctetString    = 0x000A,
	UnicodeString  = 0x000B,
	TimeOfDay      = 0x000C,
	TimeDifference = 0x000D,
	Domain         = 0x000F,
	Integer24      = 0x0010,
	Real64         = 0x0011,
	Integer40      = 0x0012,
	Integer48      = 0x0013,
	Integer56      = 0x0014,
	Integer64      = 0x0015,
	Unsigned24     = 0x0016,
	Unsigned40     = 0x0018,
	Unsigned48     = 0x0019,
	Unsigned56     = 0x001A,
	Unsigned64     = 0x001B,

	// 以下是 SSC 的合成类型，不是 CiA 基础类型
	PdoMapping     = 0x0020,	// PDO 映射入口
	Smpar          = 0x001E,	// 0x1C32/0x1C33 同步参数记录
	Identity       = 0x001F,	// 0x1018 身份记录
	Record         = 0x1000,	// 记录（仅用于结构体描述）
};

/**
 * @brief 对象条目权限位
 *
 * 位组合有意义，实际语义由 ecat_dictionary 的权限判定函数解释。
 */
namespace access {
// 位定义照 SSC sdoserv.h，这组值会原样发到 SDO Info，必须与标准一致
constexpr uint16_t kReadPreOp    = 0x0001;			// 仅 PREOP 可读
constexpr uint16_t kReadSafeOp   = 0x0002;			// 仅 SAFEOP 可读
constexpr uint16_t kReadOp       = 0x0004;			// 仅 OP 可读
constexpr uint16_t kWritePreOp   = 0x0008;			// 仅 PREOP 可写
constexpr uint16_t kWriteSafeOp  = 0x0010;			// 仅 SAFEOP 可写
constexpr uint16_t kWriteOp      = 0x0020;			// 仅 OP 可写

constexpr uint16_t kRead         = 0x0007;			// 各状态均可读（READ_PREOP|SAFEOP|OP）
constexpr uint16_t kWrite        = 0x0038;			// 各状态均可写（WRITE_PREOP|SAFEOP|OP）
constexpr uint16_t kReadWrite    = 0x003F;			// 各状态均可读写

constexpr uint16_t kRxPdoMapping = 0x0040;			// 可作为 RxPDO 映射目标
constexpr uint16_t kTxPdoMapping = 0x0080;			// 可作为 TxPDO 映射源
constexpr uint16_t kBackup       = 0x0100;			// 备份参数
constexpr uint16_t kSettings     = 0x0200;			// 设置参数
}

/**
 * @brief 对象代码（对象字典里的 ObjCode）
 */
enum class ObjectCode : uint8_t
{
	Variable = 0x07,					// 变量
	Array    = 0x08,					// 数组
	Record   = 0x09,					// 记录
};

/**
 * @brief 条目描述：一个子条目的类型与访问权限
 *
 * 对应 SSC 的 TSDOINFOENTRYDESC，bit_length 是位长度不是字节。
 */
struct EntryDescription
{
	DataType data_type;					// 数据类型
	uint16_t bit_length;				// 位长度（8 / 16 / 32 / 64）
	uint16_t access_flags;				// access 命名空间里的权限位
};

/**
 * @brief 对象条目：对象表里的一行
 *
 * 对应 SSC 的 TOBJECT。字符串与描述表都是指针，指向常量区不拷贝到对象里，
 * 这是 0 B RAM 的前提。data 对只读对象指向常量，对可写对象指向类成员。
 */
struct ObjectEntry
{
	uint16_t index;						// 对象索引（0x1000 ~ 0xFFFF）
	ObjectCode code;					// 对象代码
	uint8_t sub_count;					// 子条目数（变量为 0）
	const EntryDescription* entries;	// 子条目描述表
	const EntryDescription* sub0_entry;	// 子索引 0 的描述（数组/记录用）
	const char* name;					// 名称串（多个名字以 '\0' 分隔，末尾 0xFF）
	void* data;							// 该对象的实际存储

	void* hook_context;			// 前门的上下文

	/**
	 * @brief 访问前门（可选）
	 *
	 * 非空时读写该对象不直接碰 data 而是调用它，用于 0x10F8 这种值是算出来的对象。
	 *
	 * @param context         上下文
	 * @param index           对象索引
	 * @param sub_index       子索引
	 * @param data_size       数据字节数
	 * @param data            数据缓冲
	 * @param complete_access 是否整对象
	 * @return 0=成功，否则为中止码
	 */
	uint32_t (*access_hook)(void* context, uint16_t index, uint8_t sub_index, uint32_t data_size, void* data, bool complete_access);
};

/* PDO 映射表内容：索引 bit31..16 / 子索引 bit15..8 / 位长度 bit7..0 */
namespace pdo_mapping {

/**
 * @brief 打包一条 PDO 映射项
 * @param index      对象索引
 * @param sub_index  子索引
 * @param bit_length 位长度
 * @return 打包后的 32 位值
 */
constexpr uint32_t Make(uint16_t index, uint8_t sub_index, uint8_t bit_length)
{
	return (static_cast<uint32_t>(index) << 16) | (static_cast<uint32_t>(sub_index) << 8) | static_cast<uint32_t>(bit_length);
}

/**
 * @brief 取映射项的对象索引
 * @param mapping 打包后的映射值
 * @return 对象索引
 */
constexpr uint16_t IndexOf(uint32_t mapping)
{
	return static_cast<uint16_t>(mapping >> 16);
}

/**
 * @brief 取映射项的子索引
 * @param mapping 打包后的映射值
 * @return 子索引
 */
constexpr uint8_t SubIndexOf(uint32_t mapping)
{
	return static_cast<uint8_t>((mapping >> 8) & 0xFFU);
}

/**
 * @brief 取映射项的位长度
 * @param mapping 打包后的映射值
 * @return 位长度
 */
constexpr uint8_t BitLengthOf(uint32_t mapping)
{
	return static_cast<uint8_t>(mapping & 0xFFU);
}

}

/* 0x1C12 / 0x1C13 的子索引 0 是条目数，1..n 是 PDO 索引 */
constexpr uint8_t kMaxPdoAssignmentCount = 8;

/**
 * @brief 子索引位偏移
 *
 * SSC 的 OBJ_GetEntryOffset 返回位偏移，调用方有的 >>3 有的 >>4，
 * 看着都像"转字节"。这里只给位偏移，字节偏移用 ByteOffsetOf。
 *
 * @param entries   子条目描述表
 * @param sub_index 目标子索引（从 1 开始）
 * @return 相对于对象数据起点的位偏移
 */
constexpr uint32_t BitOffsetOf(const EntryDescription* entries, uint8_t sub_index)
{
	uint32_t bits = 0;

	for (uint8_t i = 1; i < sub_index; ++i) {
		bits += entries[i].bit_length;
	}
	return bits;
}

/**
 * @brief 子索引字节偏移
 * @param entries   子条目描述表
 * @param sub_index 目标子索引（从 1 开始）
 * @return 相对于对象数据起点的字节偏移
 */
constexpr uint32_t ByteOffsetOf(const EntryDescription* entries, uint8_t sub_index)
{
	return BitOffsetOf(entries, sub_index) / 8U;
}

/**
 * @brief 位长度转字节宽度
 *
 * 位长度不一定是 8 的整数倍（PDO 映射里可以有 1 位的布尔），所以向上取整。
 *
 * @param bit_length 位长度
 * @return 字节宽度
 */
constexpr uint16_t ByteWidthOf(uint32_t bit_length)
{
	return static_cast<uint16_t>((bit_length + 7U) / 8U);
}

}
