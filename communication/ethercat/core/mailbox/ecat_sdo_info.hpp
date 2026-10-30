/**
 * @file ecat_sdo_info.hpp
 * @author qingyu
 * @brief SDO Information 服务：对象列表、对象描述、条目描述及分片续发
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
 * @brief SDO Information 服务
 *
 * 续发用显式游标（列表类型 + 下一个对象索引），不重放上次的请求。
 */
class EcatSdoInfo final
{
public:
	using ReplySink = uint8_t (*)(void* context, uint8_t* data, uint16_t length);	// 应答提交回调

	EcatSdoInfo() = default;

	EcatSdoInfo(const EcatSdoInfo&) = delete;
	EcatSdoInfo& operator=(const EcatSdoInfo&) = delete;

	void Bind(EcatDictionary& dictionary, ReplySink sink, void* context);			// 绑定字典与应答通道

	uint8_t Handle(MailboxFrame* frame);											// 处理一帧 SDO Info 请求

	bool HasPendingFragment() const { return fragments_left_ > 0; }					// 还有未发完的分片

	uint8_t Continue(MailboxFrame* frame);											// 生成下一帧分片应答

	void Reset();																	// 清空分片状态

private:
	uint8_t HandleList(uint8_t list_type, MailboxFrame* frame, uint16_t size);		// 处理对象列表请求
	uint8_t HandleObjectDescription(MailboxFrame* frame);							// 处理对象描述请求
	uint8_t HandleEntryDescription(MailboxFrame* frame);							// 处理条目描述请求

	uint8_t HandleListFragment(MailboxFrame* frame, uint8_t list_type, const uint16_t* indices, uint16_t count, bool more);	// 写一帧列表分片并应答

	EcatDictionary* dictionary_ = nullptr;			// 对象字典
	ReplySink       sink_ 		= nullptr;			// 应答提交回调
	void*           sink_context_ = nullptr;		// 回调上下文

	uint16_t fragments_left_ = 0;					// 还剩几片
	uint16_t next_index_ = 0x1000;					// 下一片从这个对象索引开始
	uint8_t  list_type_ = 0;						// 当前列表类型
};

}
