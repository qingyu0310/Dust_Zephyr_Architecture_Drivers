/**
 * @file ecat_mailbox.cpp
 * @author qingyu
 * @brief 邮箱域传输实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "mailbox/ecat_mailbox.hpp"

#include <cstring>

#include "spec/ecat_registers.hpp"
#include "al/ecat_sm.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"
#include "log.hpp"

namespace ecat {

namespace {

constexpr uint8_t  kMaxPoolSlots = 32;		// 缓冲池位图的最大可表示块数
constexpr uint8_t  kCounterMax	 = 7;		// 邮箱头里的计数器只允许 1~7

static_assert(kMailboxBufferCount <= kMaxPoolSlots, "缓冲池大小超过 32 位位图容量");

/**
 * @brief 读 SM1 控制寄存器里的"缓冲满"状态位
 * @param esc ESC 硬件
 * @return true=发送缓冲仍满
 */
bool IsSendBufferFull(EcatEsc& esc)
{
	const SyncManagerRegisters registers = ReadSyncManager(esc, sync_manager::kMailboxIn);

	return (registers.settings & sync_manager_reg::kStatusMbxFull) != 0;
}

/**
 * @brief 读 SM0 控制寄存器里的"缓冲满"状态位
 * @param esc ESC 硬件
 * @return true=接收缓冲仍满
 */
bool IsReceiveBufferFull(EcatEsc& esc)
{
	const SyncManagerRegisters registers = ReadSyncManager(esc, sync_manager::kMailboxOut);

	return (registers.settings & sync_manager_reg::kStatusMbxFull) != 0;
}

/**
 * @brief 读主站"请求重发"位的当前电平
 * @param esc ESC 硬件
 * @return true=请求位为 1
 */
bool IsRepeatRequested(EcatEsc& esc)
{
	const SyncManagerRegisters registers = ReadSyncManager(esc, sync_manager::kMailboxIn);

	return (registers.settings & sync_manager_reg::kRepeatRequestMask) != 0;
}

/**
 * @brief 清除 SM1 的重发确认位
 * @param esc ESC 硬件
 */
void ClearRepeatAcknowledge(EcatEsc& esc)
{
	const uint16_t offset = static_cast<uint16_t>(reg::kSyncManagerBase + sync_manager::kMailboxIn * kSyncManagerRegSize + 4);
	uint8_t bytes[4] = {};

	esc.Read(offset, bytes, sizeof(bytes));

	uint32_t value = wire::ReadLe32(bytes, 0);

	value &= ~sync_manager_reg::kRepeatAck;
	wire::WriteLe32(bytes, 0, value);
	esc.Write(offset, bytes, sizeof(bytes));
}

}

/**
 * @brief 绑定协议分发
 * @param dispatcher 分发回调
 * @param context    上下文
 */
void EcatMailbox::SetDispatcher(Dispatcher dispatcher, void* context)
{
	dispatcher_ = dispatcher;
	dispatcher_context_ = context;
}

/**
 * @brief 进入 PREOP：读 SM0/SM1 配置、校验重叠、使能两个通道
 * @param esc ESC 硬件
 * @return 0=成功，否则为 AL 状态码
 */
uint16_t EcatMailbox::Start(EcatEsc& esc)
{
	esc_ = &esc;

	const SyncManagerRegisters out_registers = ReadSyncManager(esc, sync_manager::kMailboxOut);

	receive_address_ = out_registers.Address();
	receive_size_ 	 = out_registers.Length();

	const SyncManagerRegisters in_registers = ReadSyncManager(esc, sync_manager::kMailboxIn);

	send_address_    = in_registers.Address();
	send_size_ 	     = in_registers.Length();


	// 两块邮箱区不得重叠
	if (((receive_address_ + receive_size_) > send_address_) && (receive_address_ < (send_address_ + send_size_))) {
		return al_status::kInvalidMbxCfgInPreOp;
	}

	// 长度校验：SM 给的尺寸必须落在协议允许的范围内
	if (receive_size_ < kMinMailboxSize || receive_size_ > kMaxMailboxSize) {
		return al_status::kInvalidMbxCfgInPreOp;
	}
	if (send_size_ < kMinMailboxSize || send_size_ > kMaxMailboxSize) {
		return al_status::kInvalidMbxCfgInPreOp;
	}

	EnableSyncManagerChannel(esc, sync_manager::kMailboxOut);
	EnableSyncManagerChannel(esc, sync_manager::kMailboxIn);

	// 池够不够用：不够时后续每个请求都回"内存不足"
	no_memory_ = false;

	MailboxFrame probe = Acquire(receive_size_);

	if (probe.data == nullptr) {
		no_memory_ = true;
	} else {
		Release(&probe);
	}

	running_ = true;
	return al_status::kNoError;
}

/**
 * @brief 退出 PREOP：禁用通道、释放全部缓冲、清队列
 * @param esc ESC 硬件
 */
void EcatMailbox::Stop(EcatEsc& esc)
{
	running_ = false;
	esc_ = nullptr;

	DisableSyncManagerChannel(esc, sync_manager::kMailboxOut);
	DisableSyncManagerChannel(esc, sync_manager::kMailboxIn);

	// write 最独立先放；read/repeat/store 可能互相别名，池保证同一块只记一次
	if (write_frame_ != nullptr) {
		Release(write_frame_);
		write_frame_ = nullptr;
	}
	if (read_frame_ != nullptr) {
		Release(read_frame_);
		read_frame_ = nullptr;
	}
	if (repeat_frame_ != nullptr) {
		Release(repeat_frame_);
		repeat_frame_ = nullptr;
	}
	if (store_frame_ != nullptr) {
		Release(store_frame_);
		store_frame_ = nullptr;
	}

	ClearQueue(true);
	ClearQueue(false);
	ClearRepeatAcknowledge(esc);

	send_full_ 		= false;
	receive_locked_ = false;
	no_memory_ 		= false;
	repeat_toggle_ 	= false;
	read_counter_ 	= 0;
	send_head_	    = send_tail_ = 0;
	receive_head_ 	= receive_tail_ = 0;
	write_counter_ 	= 0;
}

/**
 * @brief 线程上下文：处理收到的报文、推进发送队列、处理重发请求
 *
 * 顺序照 MBX_Main：先处理接收队列，再看能否解锁接收，
 * 再看 SM1 是否已被读走，最后看重发请求。
 *
 * @param esc ESC 硬件
 */
void EcatMailbox::Service(EcatEsc& esc)
{
	if (!running_) {
		return;
	}

	MailboxFrame* frame = nullptr;

	while ((frame = Dequeue(false)) != nullptr) {
		uint8_t result = 0;

		if (dispatcher_ != nullptr) {
			result = dispatcher_(dispatcher_context_, frame);
		}

		if (result != 0)
		{
			// 协议层返回错误：同一块缓冲改写成邮箱错误报文，长度 4，类型 0
			DUST_LOG_ERR("ecat: mailbox service error 0x%02x", static_cast<unsigned>(result));
			wire::WriteLe16(frame->data, mailbox::kLengthOffset,   4);
			wire::WriteLe16(frame->data, mailbox::kProtocolOffset, 0);
			wire::WriteLe16(frame->data, kMailboxHeaderSize, 	   result);
			frame->length = kMailboxHeaderSize + 4;

			if (Submit(frame) == SubmitResult::NoMemory) {
				Release(frame);
			}
		} else {
			// 协议层若已通过 Submit 把这块缓冲交给发送队列，所有权已转移，不能再释放；
			// 没交出去的（协议层不产生应答）才还回池
			if (!IsQueued(frame)) {
				Release(frame);
			}
		}
	}

	// 收包由 PDI 事件驱动（HandleAlEvents 里调 Receive）；
	// SM1 是否被主站读走只能靠状态位判断，AL 事件里不一定有位
	if (send_full_ && !IsSendBufferFull(esc)) {
		OnSendMailboxRead(esc);
	}

	// SSC：请求位与上次电平不同才是新的重发请求，不能把电平当事件
	{
		const bool requested = IsRepeatRequested(esc);

		if (requested != repeat_request_level_) {
			repeat_request_level_ = requested;
			OnRepeatRequest(esc);
		}
	}

	// 发送邮箱空闲且队列里有货，推进一帧
	if (!send_full_ && read_frame_ == nullptr)
	{
		MailboxFrame* next = Dequeue(true);

		if (next != nullptr)
		{
			if (Transmit(esc, next)) {
				read_frame_ = next;
			} else {
				Enqueue(next, true);
			}
		}
	}
}

/**
 * @brief 提交一帧待发报文
 *
 * 本函数不持有 ESC 句柄，只入队；真正写 SM1 由 Service 在下一拍完成。
 *
 * @param frame 待发报文
 * @return 提交结果
 */
EcatMailbox::SubmitResult EcatMailbox::Submit(MailboxFrame* frame)
{
	if (frame == nullptr || frame->data == nullptr) {
		return SubmitResult::NoMemory;
	}

	// 调用方的描述符可能在栈上，队列必须存池内槽位的地址
	MailboxFrame* stable = nullptr;

	for (uint8_t slot = 0; slot < kMailboxBufferCount; ++slot)
	{
		if (slots_[slot].data == frame->data) {
			stable = &slots_[slot];
			break;
		}
	}

	if (stable == nullptr) {
		return SubmitResult::NoMemory;
	}
	stable->length = frame->length;

	// 邮箱头计数器：每发一帧递增，1~7 循环（0 跳过）
	uint8_t* flags = stable->data + mailbox::kFlagsOffset;

	flags[1] = static_cast<uint8_t>(flags[1] & ~mailbox::kCounterMask);

	if ((read_counter_ & kCounterMax) == 0) {
		read_counter_ = 1;
	}
	flags[1] = static_cast<uint8_t>(flags[1] | ((read_counter_ << mailbox::kCounterShift) & mailbox::kCounterMask));

	// SSC MBX_CopyToSendMailbox：能立刻写进 SM1 就直接发，并把该帧记为 in-flight（psReadMbx）
	// 不记的话 Service 会把这块缓冲释放掉，重发所需的数据就丢了
	if (esc_ != nullptr && Transmit(*esc_, stable)) {
		read_frame_ = stable;
		++read_counter_;
		return SubmitResult::Queued;
	}

	if (!Enqueue(stable, true)) {
		return SubmitResult::NoMemory;
	}

	++read_counter_;
	return SubmitResult::Queued;
}

/**
 * @brief 从池里取一块缓冲
 * @param capacity 需要的容量
 * @return 缓冲描述；池满时 data 为 nullptr
 */
MailboxFrame EcatMailbox::Acquire(uint16_t capacity)
{
	MailboxFrame empty {};

	if (capacity > kMaxMailboxSize) {
		capacity = kMaxMailboxSize;
	}

	for (uint8_t slot = 0; slot < kMailboxBufferCount; ++slot) 
	{
		const uint32_t mask = (1UL << slot);

		if ((pool_bitmap_ & mask) != 0) 
		{
			continue;
		}
		pool_bitmap_ |= mask;

		slots_[slot].data = pool_[slot];
		slots_[slot].capacity = kMaxMailboxSize;
		slots_[slot].length = 0;

		return slots_[slot];
	}
	return empty;
}

/**
 * @brief 归还一块缓冲
 * @param frame 之前由 Acquire 取得的缓冲
 */
void EcatMailbox::Release(MailboxFrame* frame)
{
	if (frame == nullptr || frame->data == nullptr) {
		return;
	}

	for (uint8_t slot = 0; slot < kMailboxBufferCount; ++slot) 
	{
		if (slots_[slot].data == frame->data) 
		{
			pool_bitmap_ &= ~(1UL << slot);
			frame->data = nullptr;
			frame->length = 0;
			return;
		}
	}
}

/**
 * @brief 该缓冲是否仍被持有
 * @param frame 缓冲描述
 * @return true=仍被池占用
 */
bool EcatMailbox::IsHeld(const MailboxFrame* frame) const
{
	if (frame == nullptr || frame->data == nullptr) {
		return false;
	}

	for (uint8_t slot = 0; slot < kMailboxBufferCount; ++slot) 
	{
		if (slots_[slot].data == frame->data) {
			return (pool_bitmap_ & (1UL << slot)) != 0;
		}
	}
	return false;
}

/**
 * @brief 该缓冲是否已在发送队列里
 * @param frame 缓冲描述
 * @return true=已在发送队列
 */
bool EcatMailbox::IsQueued(const MailboxFrame* frame) const
{
	if (frame == nullptr) {
		return false;
	}

	// 所有权已转移的两处：发送队列里、或正在 SM1 里等主站读走（read_frame_）
	if (frame == read_frame_ || frame == repeat_frame_ || frame == store_frame_) {
		return true;
	}

	const uint16_t capacity = kMailboxQueueDepth + 1;

	for (uint16_t i = send_head_; i != send_tail_; i = static_cast<uint16_t>((i + 1) % capacity))
	{
		if (send_queue_[i] == frame) {
			return true;
		}
	}
	return false;
}

/**
 * @brief 检查并取一帧主站写来的报文
 * @param esc ESC 硬件
 */
void EcatMailbox::Receive(EcatEsc& esc)
{
	// 邮箱没在运行（未进 PREOP 或已退出）时，旧事件不能再访问保存的地址
	if (!running_) {
		return;
	}

	// 发送侧还有未完成的工作时，先不取新报文
	if (send_full_) {
		receive_locked_ = true;
		return;
	}

	receive_locked_ = false;

	// SSC：读 4 字节取长度，这个读动作本身也承认了事件
	uint32_t header = 0;

	esc.Read(receive_address_, &header, sizeof(header));

	const uint16_t payload_length = static_cast<uint16_t>(header & 0xFFFFU);
	// 用 32 位算加法，避免 payload_length 接近 0xFFFF 时回绕成很小的帧
	const uint32_t total_length = static_cast<uint32_t>(payload_length) + kMailboxHeaderSize;

	// 小于最小头长度：非法帧，承认事件后丢弃
	if (total_length < kMailboxHeaderSize || total_length > receive_size_) {
		AcknowledgeSyncManagerArea(esc, receive_address_, receive_size_);
		return;
	}

	const uint16_t frame_length = static_cast<uint16_t>(total_length);

	MailboxFrame frame = Acquire(receive_size_);

	if (frame.data == nullptr) {
		no_memory_ = true;
		receive_locked_ = true;
		return;
	}
	no_memory_ = false;

	esc.Read(receive_address_, frame.data, frame_length);
	frame.length = frame_length;

	// 单缓冲模式下 SM0 会被标成"满"，读一次区域末尾解锁
	if (IsReceiveBufferFull(esc)) {
		AcknowledgeSyncManagerArea(esc, receive_address_, receive_size_);
	}

	// 邮箱计数未变 = 主站没收到应答，在重发同一帧，不能再处理一次
	const uint8_t counter = static_cast<uint8_t>((frame.data[mailbox::kProtocolOffset] & mailbox::kCounterMask) >> mailbox::kCounterShift);

	if (counter != 0 && counter == write_counter_) {
		Release(&frame);
		return;
	}
	write_counter_ = counter;

	// 队列存的是 slots_ 里的稳定地址，不是栈上的 frame 副本
	MailboxFrame* stable = nullptr;

	for (uint8_t slot = 0; slot < kMailboxBufferCount; ++slot)
	{
		if (slots_[slot].data == frame.data) {
			stable = &slots_[slot];
			break;
		}
	}

	if (stable == nullptr) {
		Release(&frame);
		no_memory_ = true;
		receive_locked_ = true;
		return;
	}

	// frame 是 Acquire 返回的副本，长度要同步到池内槽位
	stable->length = frame_length;

	if (!Enqueue(stable, false))
	{
		Release(&frame);
		no_memory_ = true;
		receive_locked_ = true;
	}
}

/**
 * @brief 把一帧写进 SM1
 * @param esc   ESC 硬件
 * @param frame 待发报文
 * @return true=已写进
 */
bool EcatMailbox::Transmit(EcatEsc& esc, MailboxFrame* frame)
{
	if (!running_) {
		return false;
	}
	if (send_full_) {
		return false;
	}

	// mbxSize 是载荷长度，total 是含 6 字节邮箱头的总长
	const uint16_t mbx_size = static_cast<uint16_t>(frame->length - kMailboxHeaderSize);
	const uint16_t total = frame->length;

	if (total > send_size_ || total < kMailboxHeaderSize) {
		return false;
	}

	// Reset the not used mailbox memory
	uint16_t last_used_addr = static_cast<uint16_t>((send_address_ + mbx_size + kMailboxHeaderSize) & 0xFFFCU);
	uint16_t last_addr_to_reset = static_cast<uint16_t>((send_address_ + send_size_ - 4) & 0xFFFCU);
	uint32_t dummy = 0;

	while (last_used_addr < last_addr_to_reset)
	{
		esc.Write(last_used_addr, &dummy, sizeof(dummy));
		last_used_addr = static_cast<uint16_t>(last_used_addr + 4);
	}

	// 写报文
	esc.Write(send_address_, frame->data, total);

	// 检查 SM1 是否已被标满
	const SyncManagerRegisters registers = ReadSyncManager(esc, sync_manager::kMailboxIn);

	if ((registers.settings & sync_manager_reg::kStatusMbxFull) == 0)
	{
		const uint16_t bytes_left = static_cast<uint16_t>(send_size_ - (mbx_size + kMailboxHeaderSize));

		if (bytes_left < 4) {
			const uint32_t last_data_address = static_cast<uint32_t>((mbx_size + kMailboxHeaderSize) / 4);

			std::memcpy(&dummy, reinterpret_cast<const uint32_t*>(frame->data) + last_data_address, 4 - bytes_left);
		}
		esc.Write(static_cast<uint16_t>(send_address_ + send_size_ - 4), &dummy, sizeof(dummy));
	}

	send_full_ = true;
	return true;
}

/**
 * @brief 主站已读走 SM1，推进下一帧
 * @param esc ESC 硬件
 */
void EcatMailbox::OnSendMailboxRead(EcatEsc& esc)
{
	// SSC：先往 SM1 首地址写一个 DWORD 0 锁住邮箱，再推进下一帧
	const uint32_t dummy = 0;

	esc.Write(send_address_, &dummy, sizeof(dummy));

	send_full_ = false;

	// 刚才发出去的那帧成为"上一次"（供重发）；有被顶下来的 store 帧先发它
	if (repeat_frame_ != nullptr && store_frame_ == nullptr) 
	{
		Release(repeat_frame_);
		repeat_frame_ = nullptr;
	}

	repeat_frame_ = read_frame_;
	read_frame_   = nullptr;

	MailboxFrame* next = store_frame_;

	if (next != nullptr) {
		store_frame_ = nullptr;
	} else {
		next = Dequeue(true);
	}

	if (next != nullptr) 
	{
		if (Transmit(esc, next)) {
			read_frame_ = next;
		} else {
			Enqueue(next, true);
		}
	}
}

/**
 * @brief 主站请求重发上一帧
 * @param esc ESC 硬件
 */
void EcatMailbox::OnRepeatRequest(EcatEsc& esc)
{
	if (repeat_frame_ == nullptr) {
		repeat_toggle_ = !repeat_toggle_;
		return;
	}

	// 重发时若 SM1 仍满，先把它顶上来的那帧存进 store，再发重发帧
	if (send_full_ && store_frame_ == nullptr)
	{
		DisableSyncManagerChannel(esc, sync_manager::kMailboxIn);
		store_frame_ = read_frame_;
		EnableSyncManagerChannel(esc, sync_manager::kMailboxIn);

		read_frame_ = nullptr;
		send_full_ = false;
	}

	if (Transmit(esc, repeat_frame_)) {
		// 重发成功：该帧转为 in-flight，等主站读走后进 repeat_frame_
		read_frame_ = repeat_frame_;
		repeat_frame_ = nullptr;
	} else {
		// 重发失败不能丢所有权，留在 repeat_frame_ 等下次
		Enqueue(repeat_frame_, true);
		repeat_frame_ = nullptr;
	}

	// SSC：重发完成后把确认状态写回 SM1 PDI 控制的 Repeat ACK 位
	repeat_toggle_ = !repeat_toggle_;

	{
		uint8_t bytes[4] = {};
		const uint16_t offset = static_cast<uint16_t>(reg::kSyncManagerBase + sync_manager::kMailboxIn * kSyncManagerRegSize + 4);

		esc.Read(offset, bytes, sizeof(bytes));

		uint32_t value = wire::ReadLe32(bytes, 0);

		if (repeat_toggle_) {
			value |= sync_manager_reg::kRepeatAck;
		} else {
			value &= ~sync_manager_reg::kRepeatAck;
		}
		wire::WriteLe32(bytes, 0, value);
		esc.Write(offset, bytes, sizeof(bytes));
	}
}

/**
 * @brief 入队
 * @param frame   报文
 * @param to_send true=发送队列
 * @return true=成功
 */
bool EcatMailbox::Enqueue(MailboxFrame* frame, bool to_send)
{
	if (frame == nullptr) {
		return false;
	}

	const uint16_t capacity = kMailboxQueueDepth + 1;
	uint16_t& tail = to_send ? send_tail_ : receive_tail_;
	uint16_t& head = to_send ? send_head_ : receive_head_;
	MailboxFrame** queue = to_send ? send_queue_ : receive_queue_;
	const uint16_t next = static_cast<uint16_t>((tail + 1) % capacity);

	if (next == head) {
		return false;
	}

	queue[tail] = frame;
	tail = next;
	return true;
}

/**
 * @brief 从队列取一帧
 * @param from_send true=发送队列，false=接收队列
 * @return 报文；空返回 nullptr
 */
MailboxFrame* EcatMailbox::Dequeue(bool from_send)
{
	const uint16_t capacity = kMailboxQueueDepth + 1;
	uint16_t& tail = from_send ? send_tail_ : receive_tail_;
	uint16_t& head = from_send ? send_head_ : receive_head_;
	MailboxFrame** queue = from_send ? send_queue_ : receive_queue_;

	if (head == tail) {
		return nullptr;
	}

	MailboxFrame* frame = queue[head];

	queue[head] = nullptr;
	head = static_cast<uint16_t>((head + 1) % capacity);
	return frame;
}

/**
 * @brief 清空一个队列并归还其全部缓冲
 * @param to_send true=发送队列
 */
void EcatMailbox::ClearQueue(bool to_send)
{
	MailboxFrame* frame = nullptr;

	while ((frame = Dequeue(to_send)) != nullptr) {
		Release(frame);
	}
}

}
