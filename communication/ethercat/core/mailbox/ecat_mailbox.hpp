/**
 * @file ecat_mailbox.hpp
 * @author qingyu
 * @brief 邮箱域传输：SM0/SM1 帧收发、队列、重发、缓冲所有权
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "spec/ecat_limits.hpp"
#include "interface/ecat_esc.hpp"

namespace ecat {

/**
 * @brief 一帧邮箱报文
 *
 * 缓冲是池里的一块，长度含 6 字节邮箱头。
 */
struct MailboxFrame
{
	uint8_t* data = nullptr;	// 报文首地址（池内）
	uint16_t capacity = 0;		// 该块容量
	uint16_t length = 0;		// 有效字节数（含头）
};

/**
 * @brief 邮箱域
 *
 * 不知道 CoE 存在，只认一个 Dispatcher 回调；用静态缓冲池，不碰堆。
 */
class EcatMailbox final
{
public:
	using Dispatcher = uint8_t (*)(void* context, MailboxFrame* frame);	// 协议分发回调，返回 0 表示已处理

	enum class SubmitResult : uint8_t
	{
		Queued,		// 已入发送队列，所有权归邮箱
		NoMemory,	// 队列已满，所有权仍在调用方
	};

	EcatMailbox() = default;

	EcatMailbox(const EcatMailbox&) = delete;
	EcatMailbox& operator=(const EcatMailbox&) = delete;

	void SetDispatcher(Dispatcher dispatcher, void* context);	// 绑定协议分发

	uint16_t Start(EcatEsc& esc);								// 进入 PREOP：读 SM0/SM1、校验重叠、使能两个通道
	void Stop(EcatEsc& esc);									// 退出 PREOP：禁用通道、释放缓冲、清队列

	void Service(EcatEsc& esc);									// 线程上下文：处理接收队列、推发送队列、处理重发
	void Receive(EcatEsc& esc);									// PDI 上下文：主站写满 SM0 后取回报文
	void OnSendMailboxRead(EcatEsc& esc);						// PDI 上下文：主站读走 SM1，推进下一帧

	SubmitResult Submit(MailboxFrame* frame);					// 提交一帧待发报文，成功后所有权归邮箱
	MailboxFrame Acquire(uint16_t capacity);					// 从池里取一块缓冲，池满时 data 为 nullptr
	void Release(MailboxFrame* frame);							// 归还一块缓冲

	bool IsRunning() const { return running_; }					// 邮箱通道是否已使能

private:
	bool Transmit(EcatEsc& esc, MailboxFrame* frame);			// 把一帧写进 SM1
	void OnRepeatRequest(EcatEsc& esc);							// 主站请求重发上一帧
	MailboxFrame* Dequeue(bool from_send);						// 从队列取一帧，空返回 nullptr
	bool Enqueue(MailboxFrame* frame, bool to_send);			// 入队
	void ClearQueue(bool to_send);								// 清空一个队列并归还全部缓冲
	bool IsHeld(const MailboxFrame* frame) const;				// 该缓冲是否仍被持有
	bool IsQueued(const MailboxFrame* frame) const;				// 该缓冲是否已在发送队列里

	EcatEsc* esc_ = nullptr;										// ESC 硬件，Start 后有效
	bool     running_ = false;										// 邮箱通道已使能
	uint16_t receive_address_ = 0;									// SM0 起始地址
	uint16_t receive_size_ = 0;										// SM0 长度
	uint16_t send_address_ = 0;										// SM1 起始地址
	uint16_t send_size_ = 0;										// SM1 长度

	uint8_t      pool_[kMailboxBufferCount][kMaxMailboxSize] = {};	// 静态缓冲池
	uint32_t     pool_bitmap_ = 0;									// 1=已占用
	MailboxFrame slots_[kMailboxBufferCount] = {};					// 池内每块的描述

	MailboxFrame* send_queue_[kMailboxQueueDepth + 1] = {};			// 发送队列（环形）
	MailboxFrame* receive_queue_[kMailboxQueueDepth + 1] = {};		// 接收队列（环形）
	uint16_t      send_head_ = 0;									// 发送队列头
	uint16_t      send_tail_ = 0;									// 发送队列尾
	uint16_t      receive_head_ = 0;								// 接收队列头
	uint16_t      receive_tail_ = 0;								// 接收队列尾

	MailboxFrame* write_frame_  = nullptr;							// 正在处理的接收帧
	MailboxFrame* read_frame_   = nullptr;							// 已写进 SM1、等主站读走的帧
	MailboxFrame* repeat_frame_ = nullptr;							// 上一帧，供重发
	MailboxFrame* store_frame_  = nullptr;							// 重发期间被顶下来的待发帧

	bool    send_full_ 	    = false;								// SM1 已被写、主站未读走
	bool    receive_locked_ = false;								// 发送未完成，暂不处理接收
	bool    no_memory_ 		= false;								// 缓冲池不足，对后续请求回错误
	bool    repeat_toggle_ 	= false;								// 重发确认位
	bool    repeat_request_level_ = false;							// 上一次读到的重发请求位电平（边沿比较用）
	uint8_t read_counter_ 	= 0;									// 邮箱头里的计数器（1~7 循环）
	uint8_t write_counter_ 	= 0;									// 最近一次收到报文的计数器，用于滤掉重发

	Dispatcher dispatcher_ = nullptr;								// 协议分发回调
	void*      dispatcher_context_ = nullptr;						// 回调上下文
};

}
