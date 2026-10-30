/**
 * @file ecat_wire.hpp
 * @author qingyu
 * @brief 字节序编解码与邮箱/CoE/SDO 报文字段视图
 * @version 0.1
 * @date 2026-10-30
 *
 * EtherCAT 线上字节序全是小端。这里不定义报文结构体——结构体会带 padding、
 * 非对齐访问在 RISC-V 上可能异常、且同一字节常被不同协议解释。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace ecat {

namespace wire {

/**
 * @brief 从缓冲读 16 位小端
 * @param data   源缓冲
 * @param offset 字节偏移
 * @return 组装后的值
 */
constexpr uint16_t ReadLe16(const uint8_t* data, uint16_t offset)
{
	return static_cast<uint16_t>(data[offset]) | static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
}

/**
 * @brief 从缓冲读 32 位小端
 * @param data   源缓冲
 * @param offset 字节偏移
 * @return 组装后的值
 */
constexpr uint32_t ReadLe32(const uint8_t* data, uint16_t offset)
{
	return static_cast<uint32_t>(data[offset])
		| (static_cast<uint32_t>(data[offset + 1]) << 8)
		| (static_cast<uint32_t>(data[offset + 2]) << 16)
		| (static_cast<uint32_t>(data[offset + 3]) << 24);
}

/**
 * @brief 从缓冲读 64 位小端
 * @param data   源缓冲
 * @param offset 字节偏移
 * @return 组装后的值
 */
constexpr uint64_t ReadLe64(const uint8_t* data, uint16_t offset)
{
	return static_cast<uint64_t>(ReadLe32(data, offset))
		| (static_cast<uint64_t>(ReadLe32(data, static_cast<uint16_t>(offset + 4))) << 32);
}

/**
 * @brief 向缓冲写 16 位小端
 * @param data   目标缓冲
 * @param offset 字节偏移
 * @param value  待写入的值
 */
constexpr void WriteLe16(uint8_t* data, uint16_t offset, uint16_t value)
{
	data[offset]     = static_cast<uint8_t>(value & 0xFFU);
	data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

/**
 * @brief 向缓冲写 32 位小端
 * @param data   目标缓冲
 * @param offset 字节偏移
 * @param value  待写入的值
 */
constexpr void WriteLe32(uint8_t* data, uint16_t offset, uint32_t value)
{
	data[offset]     = static_cast<uint8_t>(value & 0xFFU);
	data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
	data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
	data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

/**
 * @brief 向缓冲写 64 位小端
 * @param data   目标缓冲
 * @param offset 字节偏移
 * @param value  待写入的值
 */
constexpr void WriteLe64(uint8_t* data, uint16_t offset, uint64_t value)
{
	WriteLe32(data, offset, static_cast<uint32_t>(value & 0xFFFFFFFFULL));
	WriteLe32(data, static_cast<uint16_t>(offset + 4), static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFULL));
}

}

/* 邮箱头：长度 2 B / 地址 2 B / 标志 2 B。协议类型在 Flags[1] 低四位，即缓冲第 5 字节 */
namespace mailbox {
constexpr uint16_t kLengthOffset  = 0;
constexpr uint16_t kAddressOffset = 2;
constexpr uint16_t kFlagsOffset   = 4;

constexpr uint16_t kServiceTypeOffset = 4;		// Flags[0]：服务类型（SDO 请求/应答等）
constexpr uint16_t kProtocolOffset    = 5;		// Flags[1]：低四位协议类型，高四位计数器
constexpr uint8_t  kProtocolMask      = 0x0F;
constexpr uint8_t  kProtocolShift     = 0;
constexpr uint8_t  kCounterMask       = 0xF0;
constexpr uint8_t  kCounterShift      = 4;

/* 错误码写在邮箱错误应答的载荷首两字节，主站按它判断失败原因 */
constexpr uint8_t kErrorSyntax              = 0x01;	// 语法错误
constexpr uint8_t kErrorUnsupportedProtocol = 0x02;	// 不支持的协议
constexpr uint8_t kErrorInvalidChannel      = 0x03;	// 无效通道
constexpr uint8_t kErrorServiceNotSupported = 0x04;	// 服务不支持
constexpr uint8_t kErrorInvalidHeader       = 0x05;	// 头非法
constexpr uint8_t kErrorSizeTooShort        = 0x06;	// 尺寸过短
constexpr uint8_t kErrorNoMoreMemory        = 0x07;	// 缓冲不足
constexpr uint8_t kErrorInvalidSize         = 0x08;	// 尺寸非法
constexpr uint8_t kErrorServiceInWork       = 0x09;	// 服务处理中
}

/* CoE 头：服务号在 bits 12..15 */
namespace coe {
constexpr uint16_t kHeaderOffset = 0;	// 相对 CoE 报文头
constexpr uint16_t kHeaderSize   = 2;

constexpr uint16_t kServiceMask  = 0xF000;
constexpr uint8_t  kServiceShift = 12;

constexpr uint8_t kServiceEmergency   = 0x01;	// 应急报文（本从站不产生）
constexpr uint8_t kServiceSdoRequest  = 0x02;	// SDO 请求（含分段请求）
constexpr uint8_t kServiceSdoResponse = 0x03;	// SDO 应答
constexpr uint8_t kServiceTxPdo       = 0x04;	// TxPDO（本从站不支持）
constexpr uint8_t kServiceRxPdo       = 0x05;	// RxPDO（本从站不支持）
constexpr uint8_t kServiceTxPdoRemReq = 0x06;	// TxPDO 映射请求（不支持）
constexpr uint8_t kServiceRxPdoRemReq = 0x07;	// RxPDO 映射请求（不支持）
constexpr uint8_t kServiceSdoInfo     = 0x08;	// SDO Information

constexpr uint16_t kPayloadOffset = 6 + 2;	// 邮箱头 6 B + CoE 头 2 B
}

/* SDO 报文头：命令 1 B / 索引 2 B / 子索引 1 B / 数据 4 B */
namespace sdo {
constexpr uint16_t kCommandOffset  = 0;	// 相对 SDO 报文头
constexpr uint16_t kIndexOffset    = 1;
constexpr uint16_t kSubIndexOffset = 3;
constexpr uint16_t kDataOffset     = 4;	// expedited 数据紧跟命令/索引/子索引
constexpr uint16_t kHeaderSize     = 4;	// SSC INITSDO_HEADER_SIZE：命令 1 + 索引 2 + 子索引 1
constexpr uint16_t kCompleteSizeOffset = 4;	// normal 应答里 CompleteSize 紧跟 SDO 头

constexpr uint8_t kServiceMask  = 0xE0;	// 命令字节的服务类型（高三位）
constexpr uint8_t kServiceShift = 5;

constexpr uint8_t kServiceDownloadSegment = 0x00;	// 分段下载请求
constexpr uint8_t kServiceUploadSegment   = 0x03;	// 分段上传请求
constexpr uint8_t kServiceDownload        = 0x01;	// 初始化下载请求
constexpr uint8_t kServiceUpload          = 0x02;	// 初始化上传请求
constexpr uint8_t kServiceAbort           = 0x04;	// 中止

constexpr uint8_t kServiceInitiateDownloadRes = 0x03;	// 初始化下载应答
constexpr uint8_t kServiceDownloadSegmentRes  = 0x01;	// 分段下载应答
constexpr uint8_t kServiceUploadSegmentRes    = 0x00;	// 分段上传应答

constexpr uint8_t kExpedited      = 0x02;	// 数据全在 4 字节里
constexpr uint8_t kSizeIndicated  = 0x01;	// bit0 有效：长度字段有意义
constexpr uint8_t kExpSizeMask    = 0x0C;	// expedited 时长度为 4 减去该字段
constexpr uint8_t kExpSizeShift   = 2;
constexpr uint8_t kCompleteAccess = 0x10;	// 整对象访问
constexpr uint8_t kToggleBit      = 0x10;	// 分段 toggle

constexpr uint8_t  kSegmentNoMoreFollows = 0x01;	// 已是最后一段
constexpr uint8_t  kSegmentSizeMask      = 0x0E;	// 最后一段的有效字节数编码
constexpr uint8_t  kSegmentSizeShift     = 1;
constexpr uint8_t  kSegmentToggle        = 0x10;	// toggle 位
constexpr uint8_t  kSegmentHeaderSize    = 1;
constexpr uint16_t kMinSegmentedData     = 7;		// 少于它走 expedited

constexpr uint16_t kAbortCodeOffset = 4;	// 中止应答里，索引/子索引之后

constexpr uint32_t kAbortNonexistentObject   = 0x06020000;	// 索引不存在
constexpr uint32_t kAbortSubIndexNotExisting = 0x06090011;	// 子索引不存在
constexpr uint32_t kAbortUnsupportedAccess   = 0x06010001;	// 不支持访问
constexpr uint32_t kAbortReadOnly            = 0x06010002;	// 只读
constexpr uint32_t kAbortNotInThisState      = 0x08000022;	// 当前状态不允许访问
constexpr uint32_t kAbortParamLengthTooLong  = 0x06070012;	// 数据过长
constexpr uint32_t kAbortParamLengthTooShort = 0x06070013;	// 数据过短
constexpr uint32_t kAbortSubindexMismatch    = 0x06090030;	// 子索引与 CA 起始不匹配
constexpr uint32_t kAbortGeneralError        = 0x08000000;	// 通用错误
constexpr uint32_t kAbortNoMoreMemory        = 0x05030000;	// 缓冲不足
}

/* SDO Information：命令 1 B + 操作 1 B */
namespace sdo_info {
constexpr uint16_t kRequestOffset   = 0;
constexpr uint16_t kOperationOffset = 1;
constexpr uint16_t kHeaderSize      = 2;

constexpr uint8_t kCommandRequest  = 0x01;
constexpr uint8_t kCommandResponse = 0x02;

/* SSC：InfoHead 是 16 位，低 7 位 opcode，bit7 是 Incomplete */
constexpr uint16_t kOpcodeMask   = 0x007F;
constexpr uint16_t kIncomplete   = 0x0080;

constexpr uint8_t kOpGetOdList            = 0x01;	// 取对象列表（请求）
constexpr uint8_t kOpGetOdListResponse    = 0x02;	// 取对象列表（应答）
constexpr uint8_t kOpGetObjectDescription = 0x03;	// 取对象描述（请求）
constexpr uint8_t kOpObjectDescriptionResponse = 0x04;	// 取对象描述（应答）
constexpr uint8_t kOpGetEntryDescription  = 0x05;	// 取条目描述（请求）
constexpr uint8_t kOpEntryDescriptionResponse = 0x06;	// 取条目描述（应答）

constexpr uint8_t kListAll          = 0;	// 全部对象
constexpr uint8_t kListRxPdoAssign  = 1;	// 接收 PDO
constexpr uint8_t kListTxPdoAssign  = 2;	// 发送 PDO
constexpr uint8_t kListRxPdoMapping = 3;	// 接收映射
constexpr uint8_t kListTxPdoMapping = 4;	// 发送映射
constexpr uint8_t kListStartup      = 5;	// 启动参数

constexpr uint8_t  kMoreFollowsBit = 0x01;	// 应答头里：还有后续分片
constexpr uint16_t kFragmentOffset = 2;		// 后续位 + 保留 + 数据长度
}

}
