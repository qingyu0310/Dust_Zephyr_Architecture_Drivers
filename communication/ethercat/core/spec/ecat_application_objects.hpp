/**
 * @file ecat_application_objects.hpp
 * @author qingyu
 * @brief 对象字典的全部常量定义：标准对象 + 应用对象
 * @version 0.1
 * @date 2026-10-30
 *
 * 来源：
 *   - 标准对象：`ssc/coeappl.c` 的 GenObjDic 与各 sEntryDesc/asEntryDesc
 *   - 应用对象：`ssc/digital_ioObjects.h`（ET9300 生成，本文件手工重写为 constexpr）
 *
 * 与 C 版的关键差别：**全部 constexpr，0 B RAM**。
 *
 * C 版的对象表一半是 `OBJCONST`（进 Flash），一半是普通变量（进 RAM）：
 *   - `sEntryDesc0x1008` 这些是 `OBJCONST`，本来就在 Flash；
 *   - 但 `OutputCounterProcessDataMapping0x1600`、`sRxPDOassign`、
 *     `InputCounter0x6000` 这些对象**数据**是普通变量，占 RAM；
 *   - 更麻烦的是 `aName0x*` 虽然 const，却靠 `PROTO` 宏在 `_DIGITAL_IO_`
 *     被定义的编译单元里展开成定义、在别处展开成 extern，一个文件被 include 两次。
 *
 * 这里把它们拆成两类，边界清楚：
 *   - `kSchema`：只读的结构常量（描述表、名称串、映射表、分配表），constexpr；
 *   - `ObjectData`：可以被主站写的对象数据，作为类成员存在（由 `EcatDictionary` 持有）。
 *
 * 名称串说明：SSC 用一个 `char[]` 装"对象名 + 各子条目名"，以 `\0` 分隔，
 * 末尾用 `\377`（0xFF）标记结束。这段字节必须**逐字节**保持一致，
 * 因为主站做 SDO Info 查询时拿到的是原始字节，改了主站显示的条目名就变了。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "spec/ecat_limits.hpp"
#include "spec/ecat_object.hpp"

namespace ecat {
/**
 * @brief 对象字典里的固定索引
 *
 * 写死成常量而不是散落的字面量，是为了让"读 0x1C12 判断 PDO 分配"
 * 这类代码一眼能看出在访问哪个对象。
 */
namespace index {
constexpr uint16_t kDeviceType           = 0x1000;
constexpr uint16_t kErrorRegister        = 0x1001;
constexpr uint16_t kDeviceName           = 0x1008;
constexpr uint16_t kHardwareVersion      = 0x1009;
constexpr uint16_t kSoftwareVersion      = 0x100A;
constexpr uint16_t kIdentity             = 0x1018;
constexpr uint16_t kErrorSettings        = 0x10F1;
constexpr uint16_t kTimestamp            = 0x10F8;
constexpr uint16_t kRxPdoMapping         = 0x1600;
constexpr uint16_t kTxPdoMapping         = 0x1A00;
constexpr uint16_t kSyncManagerType      = 0x1C00;
constexpr uint16_t kRxPdoAssignment      = 0x1C12;
constexpr uint16_t kTxPdoAssignment      = 0x1C13;
constexpr uint16_t kSyncManagerOutPara   = 0x1C32;
constexpr uint16_t kSyncManagerInPara    = 0x1C33;
constexpr uint16_t kMailboxData          = 0x2000;
constexpr uint16_t kInputCounter         = 0x6000;
constexpr uint16_t kOutputCounter        = 0x7010;
constexpr uint16_t kModularDeviceProfile = 0xF000;
}

// 对象 0x1000 的值：设备类型（DEVICE_PROFILE_TYPE）
constexpr uint32_t kDeviceProfileType = 0x00001389;


/**
 * @brief 只读的对象结构常量
 *
 * 名称里的 `Schema` 是刻意的：这些是"对象的形状"，不是"对象的值"。
 */
namespace schema {
/**
 * @brief 0x1000 设备类型
 */
constexpr EntryDescription kEntry0x1000[] = {
 	{ DataType::Unsigned32, 0x20, access::kRead }, };
constexpr char kName0x1000[] = "Device type";

/**
 * @brief 0x1001 错误寄存器
 */
constexpr EntryDescription kEntry0x1001[] = {
 	{ DataType::Unsigned8, 0x08, access::kRead }, };
constexpr char kName0x1001[] = "Error register";

/**
 * @brief 0x1008 设备名
 *
 * 位长度是 `DEVICE_NAME_LEN * 8`，即 10 * 8 = 80。
 */
constexpr EntryDescription kEntry0x1008[] = {
 	{ DataType::VisibleString, kDeviceNameLength * 8U, access::kRead }, };
constexpr char kName0x1008[] = "Device name";

/**
 * @brief 0x1009 硬件版本
 */
constexpr EntryDescription kEntry0x1009[] = {
 	{ DataType::VisibleString, kHardwareVersionLength * 8U, access::kRead }, };
constexpr char kName0x1009[] = "Manufacturer Hardware version";

/**
 * @brief 0x100A 软件版本
 */
constexpr EntryDescription kEntry0x100A[] = {
 	{ DataType::VisibleString, kSoftwareVersionLength * 8U, access::kRead }, };
constexpr char kName0x100A[] = "Manufacturer Software version";

/**
 * @brief 0x1018 身份对象
 */
constexpr EntryDescription kEntry0x1018[] = {
	{ DataType::Unsigned8,  0x08, access::kRead }, 		// 子索引 0
	{ DataType::Unsigned32, 0x20, access::kRead }, 		// 厂商 ID
	{ DataType::Unsigned32, 0x20, access::kRead }, 		// 产品码
	{ DataType::Unsigned32, 0x20, access::kRead }, 		// 版本号
	{ DataType::Unsigned32, 0x20, access::kRead }, 		// 序列号
};
constexpr char kName0x1018[] =
	"Identity Object\0"
	"Vendor ID\0"
	"Product Code\0"
	"Revision Number\0"
	"Serial Number\0"
	"\377";

/**
 * @brief 0x10F1 错误设置
 *
 * 子索引 1 是 UINT32（V5.13 由 UINT16 改成 UINT32），
 * 子索引 2 是 UINT16 且可写。这个不对称在 C 版里也有，照原样保留。
 */
constexpr EntryDescription kEntry0x10F1[] = {
	{ DataType::Unsigned8,  0x08, access::kRead },          // 子索引 0
	{ DataType::Unsigned32, 0x20, access::kRead },          // 本地错误反应
	{ DataType::Unsigned16, 0x10, access::kReadWrite },     // 同步错误计数上限
};
constexpr char kName0x10F1[] =
	"Error Settings\0"
	"Local Error Reaction\0"
	"Sync Error Counter Limit\0"
	"\377";

/**
 * @brief 0x10F8 时间戳
 *
 * 值不是存在 `data` 里，而是由访问前门算出来的（读时要先刷新 DC 时间）。
 */
constexpr EntryDescription kEntry0x10F8[] = {
 	{ DataType::Unsigned64, 0x40, access::kReadWrite | access::kTxPdoMapping }, };
constexpr char kName0x10F8[] = "Timestamp Object\0\377";

/**
 * @brief 0x1C00 同步管理器类型
 *
 * 数组对象，只有第一条描述，其余元素共用。
 * 值 `{0x04, {0x0201, 0x0403}}`：两个 SM 类型项（V5.13 起由
 * 每 SM 两字节合成，0x0201 = SM0 邮箱输出 / SM1 邮箱输入，
 * 0x0403 = SM2 过程数据输出 / SM3 过程数据输入）。
 */
constexpr EntryDescription kEntry0x1C00[] = {
	{ DataType::Unsigned8, 0x08, access::kRead }, // 子索引 0
	{ DataType::Unsigned8, 0x08, access::kRead }, // 子索引 1..n 共用
};
constexpr char kName0x1C00[] = "Sync manager type";

/** 0x1C00 的子索引 0（条目数） */
constexpr uint16_t kSyncManagerTypeCount = 4;
/** 0x1C00 的子条目数据 */
constexpr uint16_t kSyncManagerTypes[] = { 0x0201, 0x0403 };

/**
 * @brief 0x1C32 / 0x1C33 同步参数
 *
 * 两条对象共用同一张描述表。子索引 3/7/14~18 是"不支持的占位"，
 * 类型为 0（无类型）、访问为 0（无权限），访问它们会返回中止码。
 * 子索引 32 是布尔，位长度 1。
 */
constexpr EntryDescription kEntry0x1C3x[] = {
	 { DataType::Unsigned8,  0x08, access::kRead },                                // 0  条目数
	 { DataType::Unsigned16, 0x10, access::kRead | access::kWritePreOp },          // 1  同步类型
	 { DataType::Unsigned32, 0x20, access::kRead },                                // 2  周期时间
	 { DataType::Unsigned8,  0x20, 0 },                                            // 3  偏移时间（不支持）
	 { DataType::Unsigned16, 0x10, access::kRead },                                // 4  支持的同步类型
	 { DataType::Unsigned32, 0x20, access::kRead },                                // 5  最小周期
	 { DataType::Unsigned32, 0x20, access::kRead },                                // 6  计算与拷贝时间
	 { DataType::Unsigned8,  0x20, 0 },                                            // 7  最小延时（不支持）
	 { DataType::Unsigned16, 0x10, access::kReadWrite },                           // 8  取周期时间
	 { DataType::Unsigned32, 0x20, access::kRead },                                // 9  延时时间
	{ DataType::Unsigned32, 0x20, access::kReadWrite },                           // 10 Sync0 周期
	{ DataType::Unsigned16, 0x10, access::kRead },                                // 11 SM 事件丢失
	{ DataType::Unsigned16, 0x10, access::kRead },                                // 12 周期过小
	{ DataType::Unsigned16, 0x10, access::kRead },                                // 13 偏移过短计数
	{ DataType::Unsigned8,  0x10, 0 },                                            // 14 RxPDO 翻转失败（不支持）
	{ DataType::Unsigned8,  0x20, 0 },                                            // 15 最小周期距离（不支持）
	{ DataType::Unsigned8,  0x20, 0 },                                            // 16 最大周期距离（不支持）
	{ DataType::Unsigned8,  0x20, 0 },                                            // 17 最小 SM 同步距离（不支持）
	{ DataType::Unsigned8,  0x20, 0 },                                            // 18 最大 SM 同步距离（不支持）
	{ DataType::Unsigned8,  0x00, 0 },                                            // 19 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 20 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 21 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 22 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 23 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 24 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 25 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 26 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 27 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 28 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 29 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 30 不存在
	{ DataType::Unsigned8,  0x00, 0 },                                            // 31 不存在
	{ DataType::Boolean,    0x01, access::kRead },                                // 32 同步错误
};

/**
 * @brief 0x1C32 名称串
 *
 * 注意中间那串连续的 `\0`：子索引 3、7、14~31 都没有名字，
 * 用空串占位。字节数必须与原数组一致。
 */
constexpr char kName0x1C32[] =
	"SM output parameter\0"
	"Synchronization Type\0"
	"Cycle Time\0"
	"\0"
	"Synchronization Types supported\0"
	"Minimum Cycle Time\0"
	"Calc and Copy Time\0"
	"\0"
	"Get Cycle Time\0"
	"Delay Time\0"
	"Sync0 Cycle Time\0"
	"SM-Event Missed\0"
	"Cycle Time Too Small\0"
	"Shift Time Too Short Counter\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"Sync Error\0"
	"\377";

/**
 * @brief 0x1C33 名称串（结构同 0x1C32，只有首项不同）
 */
constexpr char kName0x1C33[] =
	"SM input parameter\0"
	"Synchronization Type\0"
	"Cycle Time\0"
	"\0"
	"Synchronization Types supported\0"
	"Minimum Cycle Time\0"
	"Calc and Copy Time\0"
	"\0"
	"Get Cycle Time\0"
	"Delay Time\0"
	"Sync0 Cycle Time\0"
	"SM-Event Missed\0"
	"Cycle Time Too Small\0"
	"Shift Time Too Short Counter\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"Sync Error\0"
	"\377";

/**
 * @brief 过程数据对象的条目描述表
 *
 * 子索引 0 是个数项，子索引 1..N 是等宽 32 位元素。
 *
 * @tparam N 元素个数
 */
template <uint8_t N>
struct PdoEntryTable
{
	EntryDescription items[N + 1];
};

/**
 * @brief 按字数生成过程数据条目表
 * @tparam N 元素个数
 * @param flags 元素的访问权限位
 * @return 条目表，第 0 项为个数项
 */
template <uint8_t N>
constexpr PdoEntryTable<N> MakePdoEntryTable(uint16_t flags)
{
	PdoEntryTable<N> table {};

	table.items[0] = { DataType::Unsigned8, 0x08, access::kRead };

	for (uint8_t i = 1; i <= N; ++i) {
		table.items[i] = { DataType::Unsigned32, 0x20, flags };
	}

	return table;
}

/**
 * @brief PDO 映射表
 *
 * @tparam N 条目数
 */
template <uint8_t N>
struct PdoMappingTable
{
	uint32_t items[N];
};

/**
 * @brief 生成 PDO 映射表
 * @tparam N 条目数
 * @param index 被映射对象的索引
 * @return 映射表，第 i 项映射 index:(i+1):32
 */
template <uint8_t N>
constexpr PdoMappingTable<N> MakePdoMappingTable(uint16_t index)
{
	PdoMappingTable<N> table {};

	for (uint8_t i = 0; i < N; ++i) {
		table.items[i] = pdo_mapping::Make(index, static_cast<uint8_t>(i + 1), 32);
	}

	return table;
}

/**
 * @brief 0x1600 RxPDO 映射
 *
 * 每条都是 0x7010:0n:32，条目数由 RxPDO 布局决定。
 */
constexpr char kName0x1600[] =
	"OutputCounter process data mapping\0"
	"SubIndex 001\0"
	"\377";

constexpr PdoEntryTable<kProcessOutputWords> kTable0x1600 = MakePdoEntryTable<kProcessOutputWords>(access::kRead);
constexpr PdoMappingTable<kProcessOutputWords> kMappingTable0x1600 = MakePdoMappingTable<kProcessOutputWords>(0x7010);

constexpr const EntryDescription* kEntry0x1600 = kTable0x1600.items;
constexpr const uint32_t* kMapping0x1600 = kMappingTable0x1600.items;

/**
 * @brief 0x1A00 TxPDO 映射
 *
 * 每条都是 0x6000:0n:32，条目数由 TxPDO 布局决定。
 */
constexpr char kName0x1A00[] =
	"InputCounter process data mapping\0"
	"SubIndex 001\0"
	"\377";

constexpr PdoEntryTable<kProcessInputWords> kTable0x1A00 = MakePdoEntryTable<kProcessInputWords>(access::kRead);
constexpr PdoMappingTable<kProcessInputWords> kMappingTable0x1A00 = MakePdoMappingTable<kProcessInputWords>(0x6000);

constexpr const EntryDescription* kEntry0x1A00 = kTable0x1A00.items;
constexpr const uint32_t* kMapping0x1A00 = kMappingTable0x1A00.items;

/**
 * @brief 0x1C12 SM2 分配
 *
 * 子索引 0 = 1，子索引 1 = 0x1600。
 */
constexpr EntryDescription kEntry0x1C12[] = {
	{ DataType::Unsigned8,  0x08, access::kRead },
 	{ DataType::Unsigned16, 0x10, access::kRead }, };
constexpr char kName0x1C12[] = "SyncManager 2 assignment\0\377";
constexpr uint16_t kAssignment0x1C12[] = { 0x1600 };

/**
 * @brief 0x1C13 SM3 分配
 *
 * 子索引 0 = 1，子索引 1 = 0x1A00。
 */
constexpr EntryDescription kEntry0x1C13[] = {
	{ DataType::Unsigned8,  0x08, access::kRead },
 	{ DataType::Unsigned16, 0x10, access::kRead }, };
constexpr char kName0x1C13[] = "SyncManager 3 assignment\0\377";
constexpr uint16_t kAssignment0x1C13[] = { 0x1A00 };

/**
 * @brief 0x6000 输入计数器
 *
 * 可被映射进 TxPDO，只读。
 */
constexpr char kName0x6000[] = "InputCounter\0\377";

constexpr PdoEntryTable<kProcessInputWords>  kTable0x6000 = MakePdoEntryTable<kProcessInputWords>(access::kRead | access::kTxPdoMapping);
constexpr PdoEntryTable<kProcessOutputWords> kTable0x7010 = MakePdoEntryTable<kProcessOutputWords>(access::kReadWrite | access::kRxPdoMapping);

constexpr const EntryDescription* kEntry0x6000 = kTable0x6000.items;
constexpr const EntryDescription* kEntry0x7010 = kTable0x7010.items;

/**
 * @brief 0x7010 输出计数器
 *
 * 可被映射进 RxPDO，可读写。
 */
constexpr char kName0x7010[] = "OutputCounter\0\377";

/**
 * @brief 0x2000 邮箱数据
 */
constexpr EntryDescription kEntry0x2000[] = {
	{ DataType::Unsigned32, 0x20, access::kReadWrite }, };
constexpr char kName0x2000[] = "MyMbxData\0\377";

/**
 * @brief 0xF000 模块化设备配置
 */
constexpr EntryDescription kEntry0xF000[] = {
	{ DataType::Unsigned8,  0x08, access::kRead }, // 子索引 0
	{ DataType::Unsigned16, 0x10, access::kRead }, // 索引间距
	{ DataType::Unsigned16, 0x10, access::kRead }, // 最大模块数
};
constexpr char kName0xF000[] =
	"Modular Device Profile\0"
	"Index distance \0"
	"Maximum number of modules \0"
	"\377";
}

/**
 * @brief 可被主站改写的对象数据
 *
 * 这是唯一进 RAM 的这个对象字典部分。C 版里对应的是一组全局变量
 * （`sRxPDOassign`、`OutputCounter0x7010`、`u16ErrorRegister` 等）。
 *
 * 布局必须与 C 版逐字段一致——主站按 `条目偏移` 计算字节位置，
 * 少一个字段或改一个宽度，SDO Complete Access 拿到的数据就错位。
 *
 * 注意：PDO 映射表和分配表（0x1600/0x1A00/0x1C12/0x1C13）在 C 版里
 * 是可写变量，但本实现按只读处理（`access::kRead`），因为当前不存在
 * 主站改映射的业务需求。如果以后要支持，把它们从 `schema` 挪到这里。
 */
struct ObjectData
{
	/**
	 * @brief 0x1000 设备类型（只读值）
	 */
	uint32_t device_type = kDeviceProfileType;

	/**
	 * @brief 0x1001 错误寄存器
	 *
	 * C 版定义成 `UINT16` 但描述是 `UNSIGNED8`，只有低字节有效。
	 * 这里改成 `uint8_t`——保持描述表声明的宽度，避免一个对象两种宽度。
	 */
	uint8_t error_register = 0;

	/**
	 * @brief 0x1018 身份（C 版是 TOBJ1018，PACKED）
	 */
	uint16_t identity_sub_count = 4;   			// 子索引 0
	uint32_t vendor_id 		= kVendorId;
	uint32_t product_code 	= kProductCode;
	uint32_t revision 		= kRevisionNumber;
	uint32_t serial_number 	= kSerialNumber;

	/**
	 * @brief 0x10F1 错误设置（C 版是 TOBJ10F1，PACKED）
	 */
	uint16_t error_settings_sub_count = 2; 		// 子索引 0
	uint32_t local_error_reaction 	  = 0;     	// 子索引 1
	uint16_t sync_error_counter_limit = 0; 		// 子索引 2

	/**
	 * @brief 0x1C32 / 0x1C33 同步参数
	 *
	 * 字段顺序与 C 版 TSYNCMANPAR 完全一致。字段名用 `subXX` 而非语义名的地方
	 * 对应 C 版里的 `u32Si7Reserved` 之类——它们是被占位的，没有语义。
	 */
	struct SyncParameters
	{
		uint16_t sub_count  		  = 0;          // 子索引 0
		uint16_t sync_type  		  = 0;          // 子索引 1
		uint32_t cycle_time 		  = 0;          // 子索引 2
		uint32_t sub003_reserved 	  = 0;       	// 子索引 3（占位）
		uint16_t sync_types_supported = 0;  		// 子索引 4
		uint32_t min_cycle_time 	  = 0;        	// 子索引 5
		uint32_t calc_and_copy_time	  = 0;    		// 子索引 6
		uint32_t sub007_reserved      = 0;       	// 子索引 7（占位）
		uint16_t get_cycle_time       = 0;        	// 子索引 8
		uint32_t delay_time 	      = 0;          // 子索引 9
		uint32_t sync0_cycle_time     = 0;     	 	// 子索引 10
		uint16_t sm_event_missed      = 0;       	// 子索引 11
		uint16_t cycle_exceeded       = 0;        	// 子索引 12
		uint16_t shift_too_short      = 0;       	// 子索引 13
		uint16_t sub014_reserved      = 0;       	// 子索引 14（占位）
		uint32_t sub015_reserved      = 0;       	// 子索引 15（占位）
		uint32_t sub016_reserved      = 0;       	// 子索引 16（占位）
		uint32_t sub017_reserved      = 0;       	// 子索引 17（占位）
		uint32_t sub018_reserved      = 0;       	// 子索引 18（占位）
		uint8_t  sync_error           = 0;          // 子索引 32
	};

	SyncParameters sync_manager_out; 				// 0x1C32
	SyncParameters sync_manager_in; 	 			// 0x1C33

	/**
	 * @brief 0x1C00 同步管理器类型
	 */
	uint16_t sync_manager_type_count = schema::kSyncManagerTypeCount;
	uint16_t sync_manager_types[2] = { 0x0201, 0x0403 };

	/**
	 * @brief 0x2000 邮箱数据
	 */
	uint32_t mailbox_data = 0;

	/**
	 * @brief 0x6000 / 0x7010 计数器
	 */
	uint16_t input_counter_count = kProcessInputWords;
	uint32_t input_counter[kProcessInputWords] = {};
	uint16_t output_counter_count = kProcessOutputWords;
	uint32_t output_counter[kProcessOutputWords] = {};

	/**
	 * @brief 0xF000 模块化设备配置
	 */
	uint16_t modular_device_sub_count = 2;
	uint16_t index_distance = 0x0010;
	uint16_t max_modules = 0;
};

/**
 * @brief 对象表的项数
 *
 * 标准对象 11 个（0x1000/1001/1008/1009/100A/1018/10F1/10F8/1C00/1C32/1C33）
 * + 应用对象 8 个（0x1600/1A00/1C12/1C13/2000/6000/7010/F000）。
 */
constexpr uint16_t kObjectCount = 19;

}
