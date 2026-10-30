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

namespace ecat {

/**
 * @brief SDO 服务器
 *
 * 拥有一个 SDO 会话的全部状态（是否忙、分段缓冲、toggle）；
 * 不拥有邮箱（通过 ReplySink 交出应答）与字典（通过引用查询）。
 */
class EcatSdo final
{
public:
	using ReplySink = uint8_t (*)(void* context, uint8_t* data, uint16_t length);	// 应答提交回调，0=已接收

	/**
	 * @brief 分段缓冲容量
	 *
	 * 取 128 字节：最长的可分段对象是 0x1C3x（约 62 B），留一倍余量。
	 */
	static constexpr uint16_t kSegmentCapacity = 128;

	EcatSdo() = default;

	EcatSdo(const EcatSdo&) = delete;
	EcatSdo& operator=(const EcatSdo&) = delete;

	void Bind(EcatDictionary& dictionary, ReplySink sink, void* context);	// 绑定字典与应答通道

	uint8_t Handle(MailboxFrame* frame);									// 处理一帧 SDO 请求

	void Abort();															// 中止当前会话并清分段状态
	bool IsBusy() const { return in_work_; }								// 当前是否有 SDO 正在处理

private:
	uint32_t HandleInitiate(uint8_t command, MailboxFrame* frame, uint8_t* payload, uint16_t length);	// 处理初始化下载/上传请求

	uint32_t BuildUpload(MailboxFrame* frame, uint8_t* payload, uint16_t length, uint16_t index, uint8_t sub_index, bool complete_access, uint32_t object_length);	// 构造上传应答
	uint32_t BuildDownload(MailboxFrame* frame, uint8_t* payload, uint16_t length, uint16_t index, uint8_t sub_index, bool complete_access);						// 构造下载应答

	uint32_t HandleDownloadSegment(MailboxFrame* frame, uint8_t* payload, uint16_t length);				// 处理下载分段
	uint32_t HandleUploadSegment(MailboxFrame* frame, uint8_t* payload);								// 处理上传分段

	void     BuildAbort(MailboxFrame* frame, uint32_t abort_code, uint16_t index, uint8_t sub_index);	// 构造中止应答
	uint8_t  Reply(MailboxFrame* frame, uint16_t length);												// 把应答交给邮箱层

	EcatDictionary* dictionary_   = nullptr;		// 对象字典
	ReplySink       sink_         = nullptr;		// 应答提交回调
	void*           sink_context_ = nullptr;		// 回调上下文

	bool     in_work_ 		 	  = false;			// 上一个 SDO 还没处理完
	bool     last_toggle_ 	  	  = false;			// 上一次分段报文的 toggle 位
	bool     segment_follows_ 	  = false;			// 还有后续分段
	bool     segment_complete_access_ = false;		// 分段是否为整对象访问

	uint16_t segment_index_       = 0;				// 分段对象索引
	uint8_t  segment_sub_index_   = 0;				// 分段对象子索引
	uint32_t segment_total_size_  = 0;				// 本次分段传输的总字节数
	uint32_t segment_handled_     = 0;				// 已处理的字节数
	uint8_t  segment_command_     = 0;				// 当前分段属于下载还是上传

	uint8_t  segment_buffer_[kSegmentCapacity] = {};	// 分段缓冲
};

}
