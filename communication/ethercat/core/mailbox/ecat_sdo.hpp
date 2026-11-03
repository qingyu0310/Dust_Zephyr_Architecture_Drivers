/**
 * @file ecat_sdo.hpp
 * @author qingyu
 * @brief SDO 服务：请求解析、分段传输、中止应答
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "mailbox/ecat_mailbox.hpp"
#include "model/ecat_dictionary.hpp"
#include "spec/ecat_limits.hpp"

namespace ecat {

/**
 * @brief SDO 服务器
 *
 * 拥有一个 SDO 会话的全部状态（是否忙、分段缓冲、toggle）；
 * 不拥有邮箱：应答原地写在收到的帧里并返回长度，由父层统一提交。
 */
class EcatSdo final
{
public:
	/**
	 * @brief 一次请求的处理结果
	 */
	struct Result
	{
		uint8_t  code   = 0;			// 非 0 时为邮箱错误码
		uint16_t length = 0;			// 应答总字节数（含邮箱头），0=无应答
	};

	/**
	 * @brief 分段缓冲容量
	 *
	 * 映射表 CA 数据包含 2 字节条目数和每项 4 字节映射，容量按两个方向分别计算后取最大。
	 * 保留 128 字节下限供标准对象使用，邮箱仍按原有大小执行分段传输。
	 */
	static constexpr uint32_t kMappingWords    = kMaxProcessWords;
	static constexpr uint32_t kMappingCapacity = 2U + sizeof(uint32_t) * kMappingWords;
	static constexpr uint32_t kSegmentCapacity = (kMappingCapacity > 128U) ? kMappingCapacity : 128U;

	EcatSdo() = default;

	EcatSdo(const EcatSdo&) = delete;
	EcatSdo& operator=(const EcatSdo&) = delete;

	void Bind(EcatDictionary* dictionary);	// 绑定字典

	/**
	 * @brief 设置本次分发时的 AL 状态与时间戳快照
	 * @param state     当前 AL 状态
	 * @param timestamp 当前 DC 时间戳
	 */
	void SetContext(AlState state, uint64_t timestamp) { state_ = state; timestamp_ = timestamp; }

	Result Handle(MailboxFrame* frame);					// 处理一帧 SDO 请求，应答写回原帧

	void Abort();										// 中止当前会话并清分段状态
	bool IsBusy() const { return in_work_; }			// 当前是否有 SDO 正在处理

private:
	EcatDictionary* dictionary_ = nullptr;				// 对象字典

	AlState  state_     = AlState::Init;				// 本次分发时的 AL 状态快照
	uint64_t timestamp_ = 0;							// 本次分发时的 DC 时间戳快照

	bool     in_work_ 		  = false;					// 上一个 SDO 还没处理完
	bool     last_toggle_ 	  = false;					// 上一次分段报文的 toggle 位
	bool     segment_follows_ = false;					// 还有后续分段
	bool     segment_complete_access_ = false;			// 分段是否为整对象访问

	uint16_t segment_index_      = 0;					// 分段对象索引
	uint8_t  segment_sub_index_  = 0;					// 分段对象子索引
	uint32_t segment_total_size_ = 0;					// 本次分段传输的总字节数
	uint32_t segment_handled_    = 0;					// 已处理的字节数
	uint8_t  segment_command_    = 0;					// 当前分段属于下载还是上传

	uint8_t  segment_buffer_[kSegmentCapacity] = {};	// 分段缓冲
	uint16_t reply_length_ = 0;							// 本次产生的应答总长，0=无应答

	uint32_t HandleInitiate(uint8_t command, MailboxFrame* frame, uint8_t* payload, uint16_t length);	// 处理初始化下载/上传请求

	uint32_t BuildUpload  (MailboxFrame* frame, uint8_t* payload, uint16_t length, uint16_t index, uint8_t sub_index, bool complete_access, uint32_t object_length);	// 构造上传应答
	uint32_t BuildDownload(MailboxFrame* frame, uint8_t* payload, uint16_t length, uint16_t index, uint8_t sub_index, bool complete_access);							// 构造下载应答

	uint32_t HandleDownloadSegment(MailboxFrame* frame, uint8_t* payload, uint16_t length);				// 处理下载分段
	uint32_t HandleUploadSegment  (MailboxFrame* frame, uint8_t* payload);								// 处理上传分段

	void     BuildAbort(MailboxFrame* frame, uint32_t abort_code, uint16_t index, uint8_t sub_index);	// 构造中止应答
	bool     Reply(MailboxFrame* frame, uint16_t length);												// 标记应答就绪并记录长度
};

}
