/**
 * @file ecat_device.cpp
 * @author qingyu
 * @brief 从站协议组合根实现
 * @version 0.2
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 */

#include "ecat_device.hpp"

#include <zephyr/kernel.h>

#include "mailbox/ecat_coe.hpp"
#include "model/ecat_dictionary.hpp"
#include "spec/ecat_limits.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "pdo/ecat_process_data.hpp"
#include "spec/ecat_registers.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "al/ecat_sm.hpp"
#include "al/ecat_state_machine.hpp"
#include "al/ecat_sync.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"
#include "log.hpp"

namespace ecat {

EcatDevice::EcatDevice() = default;

EcatDevice::~EcatDevice() = default;

/**
 * @brief 初始化协议栈与硬件
 *
 * 顺序不能反：先把域之间的引用接好，再打开硬件。一旦 Open 成功中断随时进来，
 * 而中断路径会调 OnPdi → 状态机 → 各域，此时域还没绑定就会走进空指针。
 *
 * @param esc ESC 硬件
 * @param sii SII 介质
 * @return true=成功
 */
bool EcatDevice::Init(EcatEsc& esc)
{
	if (ready_) {
		return true;
	}

	esc_ = &esc;

	sync_.BindParameters(dictionary_.Data().sync_manager_out, dictionary_.Data().sync_manager_in);
	sync_.ResetParameters();

	state_machine_.Bind(mailbox_, process_data_, sync_, sii_service_);
	state_machine_.BindDictionary(dictionary_);

	dictionary_.BindStateQuery(QueryState, this);
	dictionary_.BindTimestamp(QueryTimestamp, this);

	sdo_.Bind(dictionary_, SubmitReply, this);
	sdo_info_.Bind(dictionary_, SubmitReply, this);

	coe_.Bind(sdo_, sdo_info_);
	mailbox_.SetDispatcher(DispatchMailbox, this);
	sii_service_.Bind(esc);

	EcatEsc::Config config {};

	if (!esc.Open(config, OnEscEvent, this)) {
		return false;
	}

	// SSC ECAT_Init：先禁用全部 SM 通道，再清 AL 事件掩码，最后置 INIT
	{
		uint8_t comm[4] = {};

		esc.Read(reg::kCommInfo, comm, sizeof(comm));

		const uint32_t comm_info = wire::ReadLe32(comm, 0);
		const uint8_t max_sm = static_cast<uint8_t>((comm_info & comm_info::kSmChannelsMask) >> comm_info::kSmChannelsShift);

		for (uint8_t i = 0; i < max_sm; ++i) {
			DisableSyncManagerChannel(esc, i);
		}

		// SSC nMaxSyncMan：状态机读剩余通道确认 SM_ACT 时要用实际通道数
		if (max_sm > 0) {
			state_machine_.SetSyncManagerCount(max_sm);
		}
	}

	state_machine_.Reset(esc);
	sii_service_.Init();

	esc.StartEvents();

	ready_ = true;
	DUST_LOG_INF("ecat: slave ready, %u sync managers", static_cast<unsigned>(state_machine_.SyncManagerCount()));
	return true;
}

/**
 * @brief 停用协议栈并关闭硬件事件
 */
void EcatDevice::Disable()
{
	if (!ready_) {
		return;
	}

	ready_ = false;

	esc_->StopEvents();
	sii_service_.Flush();
	mailbox_.Stop(*esc_);
	sync_.StopSm();

	// SSC：停用要清掉整个协议会话，不能只停事件和邮箱
	state_machine_.Reset(*esc_);
	sdo_.Abort();
	sdo_info_.Reset();
	process_data_.Clear();
}

/**
 * @brief 线程上下文：推一拍
 *
 * 顺序与 MainLoop + ECAT_Main 对应：邮箱收发、AL 事件、错误检查、链路轮询。
 *
 * @return true=仍在运行
 */
bool EcatDevice::Poll()
{
	if (!ready_) {
		return false;
	}

	mailbox_.Service(*esc_);
	HandleAlEvents(esc_->ReadAlEvent());
	state_machine_.CheckTimeout(*esc_);
	state_machine_.CheckErrors(*esc_);

	return true;
}

/**
 * @brief ISR：PDI 中断，读 AL 事件并分发
 */
void EcatDevice::OnPdi()
{
	if (!ready_) {
		return;
	}

	HandleAlEvents(esc_->ReadAlEvent());
}

/**
 * @brief ISR：SYNC0，按锁存点提交输入
 */
void EcatDevice::OnSync0()
{
	if (!ready_) {
		return;
	}

	bool input_pending = false;

	sync_.OnSync0(&input_pending);

	if (input_pending && state_machine_.IsInputRunning()) 
	{
		// 从 SM 寄存器现读地址，主站可能在 PREOP 下改过 SM 配置
		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		process_data_.PublishInputs(*esc_, registers.Address());
	}
}

/**
 * @brief ISR：SYNC1，清看门狗与锁存计数
 */
void EcatDevice::OnSync1()
{
	if (!ready_) {
		return;
	}

	sync_.OnSync1();

	// SSC Sync1_Isr：DC SYNC1 且锁存点由 SYNC1 触发时，在这里提交输入
	if (state_machine_.IsInputRunning() && process_data_.Layout().tx_size > 0 && sync_.IsInputLatchedOnSync1())
	{
		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		process_data_.PublishInputs(*esc_, registers.Address());
	}
}

/**
 * @brief 定时器：1 ms，推进状态机超时与 SII 落盘
 */
void EcatDevice::OnTick1ms()
{
	if (!ready_) {
		return;
	}

	// SSC ECAT_CheckTimer：ESM 超时递减
	state_machine_.Tick();

	// SSC ECAT_CheckTimer：DC 看门狗 + 时间戳累加
	sync_.Tick(*esc_);
	sync_.UpdateTimestamp(*esc_);

	// SSC ECAT_CheckTimer：EEPROM 延迟落盘
	sii_service_.Tick();
}

/**
 * @brief ESC 复位事件：停事件、回 INIT
 *
 * 复位之后 ESC 的 AL 状态寄存器已经是 INIT，软件不同步就是撒谎。
 */
void EcatDevice::OnReset()
{
	if (!ready_) {
		return;
	}

	esc_->StopEvents();

	// 复位后要清掉整个会话，否则残留的 SDO 分段/Info 分片会带进新会话
	mailbox_.Stop(*esc_);
	sync_.StopSm();
	state_machine_.Reset(*esc_);
	sdo_.Abort();
	sdo_info_.Reset();
	process_data_.Clear();

	// 清完再重开事件：只停不开会留下 ready_=true 但 IRQ 永久关闭
	esc_->StartEvents();
}

/**
 * @brief 当前过程数据布局
 * @return 布局快照
 */
PdoLayout EcatDevice::Layout() const
{
	return process_data_.Layout();
}

/**
 * @brief 当前 AL 状态
 * @return 状态
 */
AlState EcatDevice::State() const
{
	return state_machine_.Current();
}

/**
 * @brief 业务输出接收者
 * @param handler 回调
 * @param context 上下文
 */
void EcatDevice::SetOutputSink(OutputSink handler, void* context)
{
	process_data_.SetOutputSink(handler, context);
}

/**
 * @brief 业务输入提供者
 * @param handler 回调
 * @param context 上下文
 */
void EcatDevice::SetInputSource(InputSource handler, void* context)
{
	process_data_.SetInputSource(handler, context);
}

/**
 * @brief 硬件事件分发入口
 * @param context 本对象
 * @param event   事件源
 */
void EcatDevice::OnEscEvent(void* context, EcatEvent event)
{
	if (context == nullptr) {
		return;
	}

	auto& self = *static_cast<EcatDevice*>(context);

	switch (event)
	{
		case EcatEvent::Pdi:
			self.OnPdi();
			break;
		case EcatEvent::Sync0:
			self.OnSync0();
			break;
		case EcatEvent::Sync1:
			self.OnSync1();
			break;
		case EcatEvent::Tick1ms:
			self.OnTick1ms();
			break;
		case EcatEvent::Reset:
		default:
			self.OnReset();
			break;
	}
}

/**
 * @brief 字典的状态查询回调
 * @param context 本对象
 * @return 当前 AL 状态
 */
AlState EcatDevice::QueryState(void* context)
{
	if (context == nullptr) {
		return AlState::Init;
	}
	return static_cast<EcatDevice*>(context)->State();
}

/**
 * @brief 字典的 0x10F8 时间戳回调
 * @param context 本对象
 * @return 当前时间戳
 */
uint64_t EcatDevice::QueryTimestamp(void* context)
{
	if (context == nullptr) {
		return 0;
	}
	return static_cast<EcatDevice*>(context)->sync_.Timestamp();
}

/**
 * @brief 邮箱的协议分发回调
 * @param context 本对象
 * @param frame   收到的报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatDevice::DispatchMailbox(void* context, struct MailboxFrame* frame)
{
	if (context == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	auto& self = *static_cast<EcatDevice*>(context);

	return EcatCoe::Dispatch(&self.coe_, frame);
}

/**
 * @brief SDO 应答提交
 *
 * 应答就写在收到的那个帧的缓冲里（原地改写），缓冲本来就在池里，
 * 所以这里只需把长度改好并交给邮箱队列，由 Service 在下一拍推进。
 *
 * @param context 本对象
 * @param data    应答报文
 * @param length  应答字节数
 * @return 0=已入队
 */
uint8_t EcatDevice::SubmitReply(void* context, uint8_t* data, uint16_t length)
{
	if (context == nullptr || data == nullptr) {
		return mailbox::kErrorInvalidSize;
	}

	auto& self = *static_cast<EcatDevice*>(context);

	MailboxFrame frame {};

	frame.data = data;
	frame.length = length;
	frame.capacity = kMaxMailboxSize;

	const EcatMailbox::SubmitResult result = self.mailbox_.Submit(&frame);

	// 应答提交失败是真实故障，要能看到
	if (result != EcatMailbox::SubmitResult::Queued) {
		DUST_LOG_ERR("ecat: reply submit failed len=%u", static_cast<unsigned>(length));
	}


	return (result == EcatMailbox::SubmitResult::Queued) ? 0 : mailbox::kErrorNoMoreMemory;
}

/**
 * @brief 处理 AL 事件寄存器的各个位
 * @param events 0x0220 的值
 */
void EcatDevice::HandleAlEvents(uint16_t events)
{
	// AL Control：读 0x0120 即确认，必须整 32 位读
	if ((events & al_event::kAlControl) != 0) {
		uint8_t control[4] = {};

		esc_->Read(reg::kAlControl, control, sizeof(control));
		state_machine_.HandleControl(*esc_, control[0]);

		// SSC：处理 AL Control 时同时消费 SM_CHANGE（转换里已经检查过 SM 设置），
		// 否则同一份快照会被重复处理一次
		events = static_cast<uint16_t>(events & ~al_event::kSmChange);
	}

	// SM Change：SM 配置被主站改写，按当前状态重新检查配置
	// SSC 门控：无 pending 转换、无 AL 错误、当前不是 INIT 才单独处理
	if ((events & al_event::kSmChange) != 0 && state_machine_.CanRecheck()) {
		state_machine_.Recheck(*esc_);
	}

	// EEPROM 命令
	if ((events & al_event::kEepromCommand) != 0) {
		sii_service_.PollCommand();
	}

	// 过程数据部分与 ISR 共用同一份实现
	HandlePdiEvents(events);

	// 邮箱写事件：主站已把报文写进 SM0，立刻取回来交给协议层
	if ((events & al_event::kMailboxWrite) != 0) {
		mailbox_.Receive(*esc_);
	}

	// 邮箱读事件：主站读走了 SM1，推进下一帧
	if ((events & al_event::kMailboxRead) != 0) {
		mailbox_.OnSendMailboxRead(*esc_);
	}
}

/**
 * @brief 处理过程数据相关事件（ISR 与线程共用）
 *
 * SSC PDI_Isr 只做这些：输出映射、输入提交、事件承认。
 *
 * @param events 0x0220 的值
 */
void EcatDevice::HandlePdiEvents(uint16_t events)
{
	// 过程数据输出：无论哪个状态，收到 SM2 事件都算已收到首帧
	if ((events & al_event::kProcessOutput) != 0)
	{
		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessOut);

		state_machine_.MarkFirstOutputs(*esc_);

		if (state_machine_.IsOutputRunning())
		{
			process_data_.ConsumeOutputs(*esc_, registers.Address());
			sync_.OnProcessOutput();
		} else {
			// 非 OP 下也要承认事件，否则 ESC 不再产生下次中断
			AcknowledgeSyncManagerArea(*esc_, registers.Address(), process_data_.Layout().rx_size);
		}

		// SSC PDI_Isr：只有输入同步类型是 SM/SM2 同步时才在这里提交输入；
		// DC 模式由 SYNC0/SYNC1 锁存点提交，不能两处都发
		if (state_machine_.IsInputRunning() && process_data_.Layout().tx_size > 0 && sync_.IsInputSmSynchron())
		{
			const SyncManagerRegisters in_registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

			process_data_.PublishInputs(*esc_, in_registers.Address());
		}
	}

	// 过程数据输入：主站读走 SM3 后让缓冲重新可用
	if ((events & al_event::kProcessInput) != 0)
	{
		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		AcknowledgeSyncManagerArea(*esc_, registers.Address(), process_data_.Layout().tx_size);
	}
}

}
