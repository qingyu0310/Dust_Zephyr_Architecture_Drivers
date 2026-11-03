/**
 * @file ecat_device.cpp
 * @author qingyu
 * @brief 从站协议组合根实现
 * @version 0.3
 * @date 2026-11-03
 *
 * @copyright Copyright (c) 2026
 */

#include "ecat_device.hpp"

#include <zephyr/kernel.h>

#include "al/ecat_sm.hpp"
#include "al/ecat_state_machine.hpp"
#include "al/ecat_sync.hpp"
#include "interface/ecat_esc.hpp"
#include "log.hpp"
#include "mailbox/ecat_coe.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "model/ecat_dictionary.hpp"
#include "pdo/ecat_process_data.hpp"
#include "spec/ecat_limits.hpp"
#include "spec/ecat_registers.hpp"
#include "spec/ecat_wire.hpp"

namespace ecat {

namespace {

constexpr uint8_t kLedOff         = 0x00;	// LED 灭
constexpr uint8_t kLedBlinking    = 0x0D;	// LED 慢闪
constexpr uint8_t kLedSingleFlash = 0x01;	// LED 单闪
constexpr uint8_t kLedDoubleFlash = 0x02;	// LED 双闪
constexpr uint8_t kLedFlickering  = 0x0E;	// LED 快闪
constexpr uint8_t kLedOn          = 0x0F;	// LED 常亮
constexpr uint8_t kLedOverride    = 0x10;	// LED 覆盖位

/**
 * @brief 读 32 位寄存器
 * @param esc     ESC 硬件
 * @param address 寄存器偏移
 * @return 寄存器值
 */
uint32_t Read32(EcatEsc& esc, uint16_t address)
{
	uint8_t bytes[4] = {};

	esc.Read(address, bytes, sizeof(bytes));

	return wire::ReadLe32(bytes, 0);
}

/**
 * @brief 写 32 位寄存器
 * @param esc     ESC 硬件
 * @param address 寄存器偏移
 * @param value   待写入的值
 */
void Write32(EcatEsc& esc, uint16_t address, uint32_t value)
{
	uint8_t bytes[4] = {};

	wire::WriteLe32(bytes, 0, value);
	esc.Write(address, bytes, sizeof(bytes));
}

}

/**
 * @brief 准备协议栈：接域、开硬件、初始化到 INIT
 *
 * 不做 StartEvents：中断一旦开启，OnPdi 随时进来，此时 PHY 与业务回调可能还没装好。
 * 开启事件必须由父层在全部资源就绪后调 Start() 完成。
 *
 * @param esc     ESC 硬件
 * @param profile 过程数据布局
 * @return true=成功
 */
bool EcatDevice::Prepare(EcatEsc* esc, const DeviceProfile& profile)
{
	if (ready_ || esc == nullptr) {
		return false;
	}

	esc_ = esc;

	profile_ = profile;

	dictionary_  .SetProfile(profile_);
	process_data_.SetProfile(profile_);

	sync_.ResetParameters();

	sdo_.Bind(&dictionary_);
	sdo_info_.Bind(&dictionary_);

	sii_service_.Bind(esc_);
	diagnostics_.Bind(esc_);

	EcatEsc::Config config {};

	if (!esc_->Open(config, OnEscEvent, this)) {
		return false;
	}

	// SSC ECAT_Init：先禁用全部 SM 通道，再清 AL 事件掩码，最后置 INIT
	{
		uint8_t comm[4] = {};

		esc_->Read(reg::kCommInfo, comm, sizeof(comm));

		const uint32_t comm_info = wire::ReadLe32(comm, 0);
		const uint8_t max_sm = static_cast<uint8_t>((comm_info & comm_info::kSmChannelsMask) >> comm_info::kSmChannelsShift);

		for (uint8_t i = 0; i < max_sm; ++i) {
			DisableSyncManagerChannel(*esc_, i);
		}

		// SSC nMaxSyncMan：状态机读剩余通道确认 SM_ACT 时要用实际通道数
		if (max_sm > 0) {
			state_machine_.SetSyncManagerCount(max_sm);
		}
	}

	state_machine_.Reset();
	ResetALEventMask(0);
	Publish();
	sii_service_.Init();

	return true;
}

/**
 * @brief 启动协议栈：发布 ready 后再开硬件事件
 *
 * 这个顺序解决"StartEvents 早于 PHY 与业务回调"的窗口：事件开启时
 * ESC、PHY、业务回调都已就绪，中断进来不会碰到半成品。
 *
 * @return true=已就绪
 */
bool EcatDevice::Start()
{
	ready_ = true;
	esc_->StartEvents();

	DUST_LOG_INF("ecat: slave ready, %u sync managers", static_cast<unsigned>(state_machine_.SyncManagerCount()));
	return true;
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

	const uint32_t poll_ms = k_uptime_get_32();

	MailboxFrame* frame = nullptr;

	while ((frame = mailbox_.TakeReceived()) != nullptr) {
		mailbox_.CompleteReceived(frame, DispatchMailbox(frame));
	}
	mailbox_.Service(*esc_);
	HandleAlEvents(esc_->ReadAlEvent());

	// 转换超时：状态机判定，父层执行退态
	{
		AlStep step {};

		state_machine_.SetOutputExpected(process_data_.Layout().rx_size > 0);

		if (state_machine_.CheckTimeout(&step)) {
			ExecuteAlStep(step);
			Publish();
		}
	}

	CheckErrors();

	const bool output_running = state_machine_.IsOutputRunning();

	// 诊断只观察：采样、冻结首次故障、按需回放，不参与协议决策
	const DiagnosticSnapshot snapshot = CaptureDiagnosticSnapshot();

	diagnostics_.Poll(poll_ms, output_running, snapshot);
	diagnostics_.Report(poll_ms, snapshot, state_machine_.HasError());

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
	state_machine_.Reset();
	sdo_.Abort();
	sdo_info_.Reset();
	process_data_.Clear();
}

/**
 * @brief ISR：PDI 中断，读 AL 事件并分发
 */
void EcatDevice::OnPdi()
{
	diagnostics_.OnPdi();

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
	diagnostics_.OnTick();

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
	state_machine_.Reset();
	sdo_.Abort();
	sdo_info_.Reset();
	process_data_.Clear();

	// 清完再重开事件：只停不开会留下 ready_=true 但 IRQ 永久关闭
	esc_->StartEvents();
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
 * @brief 处理 AL 事件寄存器的各个位
 * @param events 0x0220 的值
 */
void EcatDevice::HandleAlEvents(uint16_t events)
{
	// AL Control：读 0x0120 即确认，必须整 32 位读
	if ((events & al_event::kAlControl) != 0) {
		uint8_t control[4] = {};

		esc_->Read(reg::kAlControl, control, sizeof(control));

		// 状态机判定目标态，父层执行转换
		{
			AlStep step {};

			if (state_machine_.HandleControl(control[0], &step)) {
				if (step.action != AlAction::None) {
					ExecuteAlStep(step);
				}
			}
		}

		// SSC：处理 AL Control 时同时消费 SM_CHANGE（转换里已经检查过 SM 设置），
		// 否则同一份快照会被重复处理一次
		events = static_cast<uint16_t>(events & ~al_event::kSmChange);
	}

	// SM Change：SM 配置被主站改写，按当前状态重新检查配置
	// SSC 门控：无 pending 转换、无 AL 错误、当前不是 INIT 才单独处理
	if ((events & al_event::kSmChange) != 0 && state_machine_.CanRecheck()) {
		Recheck();
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
		diagnostics_.OnSm2(k_uptime_get_32());

		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessOut);

		state_machine_.MarkFirstOutputs();

		// SSC AL_ControlRes：首帧到达时若 SAFEOP→OP 仍在 pending，立刻完成转换
		{
			AlStep step {};

			if (state_machine_.CompletePendingOp(&step)) {
				ExecuteAlStep(step);
			}
		}

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
		diagnostics_.OnSm3();

		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		AcknowledgeSyncManagerArea(*esc_, registers.Address(), process_data_.Layout().tx_size);
	}
}

/**
 * @brief 父层按 CoE 请求类型调用服务，不向邮箱注册回调
 * @param frame 收到的报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatDevice::DispatchMailbox(MailboxFrame* frame)
{
	// 字典读写由本次分发显式传入状态与时间戳快照，不再回调上层查询
	sdo_.SetContext(State(), sync_.Timestamp());
	dictionary_.ProjectSync(sync_.OutputParameters(), sync_.InputParameters());

	const EcatCoe::Selection selection = EcatCoe::Select(frame);

	if (selection.code != 0) {
		return selection.code;
	}
	if (selection.service == EcatCoe::Service::Sdo) {
		const EcatSdo::Result result = sdo_.Handle(frame);

		frame->length = result.length;
		return result.code;
	}

	const EcatSdoInfo::Result result = sdo_info_.Handle(frame);

	frame->length = result.length;
	return result.code;
}

/**
 * @brief 执行状态机给出的一步
 *
 * 状态机只判定该进哪个态、该退到哪一层，硬件与各域的操作全部集中在这里。
 *
 * @param step 决策结果
 * @return 0=成功；kAlStatusInWork=转换进行中；否则为 AL 状态码
 */
uint16_t EcatDevice::ExecuteAlStep(const AlStep& step)
{
	switch (step.action)
	{
		case AlAction::EnterPreOp:
		case AlAction::EnterSafeOp:
		case AlAction::EnterOp:
			return EnterStep(step);

		case AlAction::LeaveOp:
			return LeaveOp();

		case AlAction::LeaveSafeOp:
			return LeaveSafeOp();

		case AlAction::LeavePreOp:
			return LeavePreOp();

		case AlAction::BackToInit:
			BackToInit();
			state_machine_.SetState(AlState::Init);
			return al_status::kNoError;

		case AlAction::Publish:
			Publish();
			return al_status::kNoError;

		case AlAction::ResetEventMask:
			ResetALEventMask(0);
			return al_status::kNoError;

		case AlAction::None:
		default:
			return al_status::kNoError;
	}
}

/**
 * @brief 执行升态动作并收尾
 * @param step 决策结果
 * @return 0=成功；kAlStatusInWork=转换进行中；否则为 AL 状态码
 */
uint16_t EcatDevice::EnterStep(const AlStep& step)
{
	const uint16_t result = Transition(step.target, step.ack);

	FinishTransition(step.target, result);

	return result;
}

/**
 * @brief 按目标态逐级退、再进
 *
 * 顺序照 SSC AL_ControlInd：非法跳转先拦，再 OP→SAFEOP→PREOP→INIT 逐级退，
 * 最后执行目标态的进入动作。
 *
 * @param target 目标状态
 * @param ack    本次是否带错误确认
 * @return 0=成功；kAlStatusInWork=转换进行中；否则为 AL 状态码
 */
uint16_t EcatDevice::Transition(AlState target, bool ack)
{
	const AlState current = state_machine_.Current();

	// SSC：同状态请求时，若带错误确认则重新使能 SM，不算状态变化
	if (target == current)
	{
		if (ack && (current == AlState::SafeOp || current == AlState::Op))
		{
			if (process_data_.Layout().rx_size > 0) {
				EnableSyncManagerChannel(*esc_, sync_manager::kProcessOut);
			} else if (process_data_.Layout().tx_size > 0) {
				EnableSyncManagerChannel(*esc_, sync_manager::kProcessIn);
			}
		}
		return al_status::kNoError;
	}

	switch (current)
	{
		case AlState::Init:
			if (target == AlState::SafeOp || target == AlState::Op || target == AlState::Boot) {
				return al_status::kInvalidAlControl;
			}
			break;

		case AlState::PreOp:
			if (target == AlState::Op || target == AlState::Boot) {
				return al_status::kInvalidAlControl;
			}
			break;

		case AlState::SafeOp:
		case AlState::Op:
			if (target == AlState::Boot) {
				return al_status::kInvalidAlControl;
			}
			break;

		case AlState::Boot:
			return al_status::kBootNotSupported;

		default:
			return al_status::kUnknownAlControl;
	}

	uint16_t result = al_status::kNoError;

	// 升态前校验：PREOP→SAFEOP 先跑映射更新 PDO 长度，再校验 SM
	if (target != AlState::Init && target != AlState::Boot)
	{
		if (target == AlState::SafeOp)
		{
			result = process_data_.Configure();

			if (result != al_status::kNoError) {
				return result;
			}
		}

		// 目标 PREOP 只校验邮箱（MAILBOX_READ+1 = 2），目标 SAFEOP/OP 校验全部
		const uint8_t channel_count = (target == AlState::PreOp) ? static_cast<uint8_t>(sync_manager::kMailboxIn + 1) : static_cast<uint8_t>(sync_manager::kProcessIn + 1);

		result = CheckSmSettings(channel_count);

		if (result != al_status::kNoError) {
			return result;
		}
	}

	if (current == AlState::Op)
	{
		result = LeaveOp();
		if (result != al_status::kNoError) {
			return result;
		}
	}

	if ((current == AlState::Op || current == AlState::SafeOp) && (target == AlState::Init || target == AlState::PreOp))
	{
		result = LeaveSafeOp();
		if (result != al_status::kNoError) {
			return result;
		}
	}

	if (target == AlState::Init && (current == AlState::Op || current == AlState::SafeOp || current == AlState::PreOp))
	{
		result = LeavePreOp();
		if (result != al_status::kNoError) {
			return result;
		}
	}

	switch (target)
	{
		case AlState::Init:
			BackToInit();
			state_machine_.SetState(AlState::Init);
			return al_status::kNoError;

		case AlState::PreOp:
			result = EnterPreOp();
			break;

		case AlState::SafeOp:
			result = EnterSafeOp();
			break;

		case AlState::Op:
			result = EnterOp();
			break;

		default:
			return al_status::kUnknownAlControl;
	}

	// SSC：NOERROR_INWORK 表示转换还在进行，不是失败——保持当前状态等下一拍
	if (result == kAlStatusInWork) {
		return result;
	}

	if (result == al_status::kNoError) {
		state_machine_.SetState(target);
	} else
	{
		// SSC AL_ControlInd 失败收尾：只停掉目标态对应的那一层，保持状态与已开资源一致
		switch (target)
		{
			case AlState::Op:
				LeaveOp();
				state_machine_.SetState(AlState::SafeOp);
				break;

			case AlState::SafeOp:
				LeaveSafeOp();
				state_machine_.SetState(AlState::PreOp);
				break;

			default:
				break;
		}
	}

	return result;
}

/**
 * @brief 转换结果收尾：装载 pending、写状态码、发布
 * @param target 目标状态
 * @param result 转换结果
 */
void EcatDevice::FinishTransition(AlState target, uint16_t result)
{
	if (result == kAlStatusInWork)
	{
		state_machine_.BeginTransition(target);
	}
	else if (result == al_status::kNoError)
	{
		state_machine_.SetStatusCode(al_status::kNoError);
		DUST_LOG_INF("ecat: al_state -> 0x%02x", static_cast<unsigned>(state_machine_.Current()));
	}
	else
	{
		state_machine_.SetStatusCode(result);
		state_machine_.SetErrorLatch(true);
		DUST_LOG_ERR("ecat: al_state 0x%02x failed code=0x%04x", static_cast<unsigned>(target), static_cast<unsigned>(result));
	}

	Publish();
}

/**
 * @brief 进入 PREOP（开邮箱）
 * @return 0=成功；否则为 AL 状态码
 */
uint16_t EcatDevice::EnterPreOp()
{
	// SSC UpdateEEPROMLoadedState：EEPROM 加载失败时拒绝进 PREOP
	{
		uint8_t config[4] = {};

		esc_->Read(reg::kEepromConfig, config, sizeof(config));

		const uint32_t eeprom = wire::ReadLe32(config, 0);

		if ((eeprom & eeprom_config::kErrorCrc) != 0 || (eeprom & eeprom_config::kErrorLoad) != 0) {
			return al_status::kEeError;
		}
	}

	const uint16_t result = mailbox_.Start(*esc_);

	if (result != al_status::kNoError) {
		mailbox_.Stop(*esc_);
		return result;
	}
	return al_status::kNoError;
}

/**
 * @brief 进入 SAFEOP（备过程数据 + 起同步 + 开输入）
 * @return 0=成功；否则为 AL 状态码
 */
uint16_t EcatDevice::EnterSafeOp()
{
	// 先跑可失败的校验，失败时还没动过硬件，不会留副作用
	const uint16_t prepare = sync_.PrepareSm(*esc_, process_data_.Layout().rx_size, process_data_.Layout().tx_size);

	if (prepare != al_status::kNoError) {
		return prepare;
	}

	const uint16_t started = sync_.StartSm(*esc_);

	if (started != al_status::kNoError) {
		sync_.StopSm();
		return started;
	}

	// 校验通过后才提交硬件：清过程数据、使能 SM2/SM3
	process_data_.Clear();
	state_machine_.ClearFirstOutputs();

	if (process_data_.Layout().rx_size > 0) {
		EnableSyncManagerChannel(*esc_, sync_manager::kProcessOut);
	}
	if (process_data_.Layout().tx_size > 0) {
		EnableSyncManagerChannel(*esc_, sync_manager::kProcessIn);
	}

	// SSC StartInputHandler：保存验证过的 SM2/SM3 地址
	{
		const SyncManagerRegisters out_registers = ReadSyncManager(*esc_, sync_manager::kProcessOut);
		const SyncManagerRegisters in_registers  = ReadSyncManager(*esc_, sync_manager::kProcessIn);
		const SyncManagerRegisters mbx_out       = ReadSyncManager(*esc_, sync_manager::kMailboxOut);
		const SyncManagerRegisters mbx_in        = ReadSyncManager(*esc_, sync_manager::kMailboxIn);

		output_address_ = out_registers.Address();
		input_address_  = in_registers.Address();

		// SSC：过程数据区不得与邮箱区重叠，SM2 与 SM3 之间也不得重叠
		const uint16_t overlap = process_data_.ValidateAreas(out_registers.Address(), in_registers.Address(), mbx_out.Address(), mbx_out.Length(), mbx_in.Address(), mbx_in.Length());

		if (overlap != al_status::kNoError) {
			DisableSyncManagerChannel(*esc_, sync_manager::kProcessOut);
			DisableSyncManagerChannel(*esc_, sync_manager::kProcessIn);
			sync_.StopSm();
			return overlap;
		}
	}

	{
		const SyncManagerRegisters in_registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		process_data_.PublishInputs(*esc_, in_registers.Address());
	}

	// SSC SetALEventMask：读-改-写，且必须清掉 SYNC0/SYNC1
	uint16_t event_mask = al_event::kAlControl | al_event::kEepromCommand;

	if (process_data_.Layout().rx_size > 0) {
		event_mask |= al_event::kProcessOutput;
	} else if (process_data_.Layout().tx_size > 0) {
		event_mask |= al_event::kProcessInput;
	}

	SetALEventMask(event_mask);

	state_machine_.SetInputRunning(true);
	return al_status::kNoError;
}

/**
 * @brief 进入 OP（开输出）
 * @return 0=成功；kAlStatusInWork=等首帧；否则为 AL 状态码
 */
uint16_t EcatDevice::EnterOp()
{
	// SSC SAFEOP_2_OP：没收到首帧输出时保持 pending，等下一拍再完成，不是拒绝
	if (process_data_.Layout().rx_size > 0 && !state_machine_.HasFirstOutputs()) {
		return kAlStatusInWork;
	}

	if (process_data_.Layout().rx_size > 0) {
		EnableSyncManagerChannel(*esc_, sync_manager::kProcessOut);
	}

	state_machine_.SetOutputRunning(true);
	return al_status::kNoError;
}

/**
 * @brief 退出 OP（关输出）
 * @return 0=成功
 */
uint16_t EcatDevice::LeaveOp()
{
	state_machine_.SetOutputRunning(false);
	state_machine_.ClearFirstOutputs();
	return al_status::kNoError;
}

/**
 * @brief 退出 SAFEOP（关输入）
 * @return 0=成功
 */
uint16_t EcatDevice::LeaveSafeOp()
{
	// SSC StopInputHandler：禁用 SM2/SM3
	if (process_data_.Layout().rx_size > 0) {
		DisableSyncManagerChannel(*esc_, sync_manager::kProcessOut);
	}
	if (process_data_.Layout().tx_size > 0) {
		DisableSyncManagerChannel(*esc_, sync_manager::kProcessIn);
	}

	// 清掉 SYNC0/SYNC1/过程数据事件位
	ResetALEventMask(static_cast<uint16_t>(~(al_event::kSync0 | al_event::kSync1 | al_event::kProcessOutput | al_event::kProcessInput)));

	// 复位同步域全部状态
	sync_.StopSm();
	sync_.ResetUserSyncType();

	state_machine_.ClearFirstOutputs();
	state_machine_.SetInputRunning(false);
	return al_status::kNoError;
}

/**
 * @brief 退出 PREOP（关邮箱）
 * @return 0=成功
 */
uint16_t EcatDevice::LeavePreOp()
{
	mailbox_.Stop(*esc_);
	return al_status::kNoError;
}

/**
 * @brief 回到 INIT 的收尾
 */
void EcatDevice::BackToInit()
{
	sync_.StopSm();
	sync_.ResetUserSyncType();
	state_machine_.SetOutputRunning(false);
	state_machine_.SetInputRunning(false);
	state_machine_.ClearFirstOutputs();
}

/**
 * @brief 请求本地退态
 * @param code   AL 状态码
 * @param target 目标状态
 */
void EcatDevice::RequestLocalState(uint16_t code, AlState target)
{
	DUST_LOG_WRN("ecat local state: 0x%02x -> 0x%02x code=0x%04x", static_cast<unsigned>(state_machine_.Current()), static_cast<unsigned>(target), static_cast<unsigned>(code));

	if (state_machine_.Current() == AlState::Op && target != AlState::Op) {
		LeaveOp();
	}

	state_machine_.SetState(target);
	state_machine_.SetStatusCode(code);
	state_machine_.SetErrorLatch(true);
	Publish();
}

/**
 * @brief 周期性错误检查
 */
void EcatDevice::CheckErrors()
{
	// SSC CheckIfEcatError：只在主站配置了看门狗时间时才检查
	if (sync_.HasProcessWatchdog() && process_data_.Layout().rx_size > 0)
	{
		const uint32_t watchdog = Read32(*esc_, reg::kPdWdState);

		if ((watchdog & kPdWdTriggerMask) == 0)
		{
			if (state_machine_.IsOutputRunning()) {
				RequestLocalState(al_status::kSmWatchdog, AlState::SafeOp);
				return;
			}

			state_machine_.ClearFirstOutputs();
		}
	}

	// DC 看门狗只在 1 ms 路径推进，这里只读结果
	if (sync_.IsDcActive() && state_machine_.IsOutputRunning())
	{
		if (!sync_.IsSyncSequenceValid()) {
			RequestLocalState(al_status::kSyncError, AlState::SafeOp);
		}
	}
}

/**
 * @brief SM 配置变化后按当前状态重新检查
 */
void EcatDevice::Recheck()
{
	// SSC ECAT_Main 的 SM_CHANGE 分支只调 AL_ControlInd，不跳过 INIT
	const uint8_t channel_count = (state_machine_.Current() == AlState::PreOp) ? static_cast<uint8_t>(sync_manager::kMailboxIn + 1) : static_cast<uint8_t>(sync_manager::kProcessIn + 1);

	const uint16_t result = CheckSmSettings(channel_count);

	if (result != al_status::kNoError)
	{
		// 校验失败按当前态退一层，保留主站可观察的错误码
		RequestLocalState(result, (state_machine_.Current() == AlState::Op) ? AlState::SafeOp : AlState::PreOp);
	}
}

/**
 * @brief 校验 SM0~SM3 的配置
 *
 * 照 SSC CheckSmSettings：邮箱区校验使能位/方向/缓冲模式/长度/地址，
 * 过程数据区校验长度必须与 PDO 布局一致、方向必须正确。
 *
 * @param channel_count 需要校验的通道数量（PREOP 为 2，其余为 4）
 * @return 0=通过，否则为 AL 状态码
 */
uint16_t EcatDevice::CheckSmSettings(uint8_t channel_count) const
{
	uint16_t result = al_status::kNoError;

	// SM0：接收邮箱
	const SyncManagerRegisters mbx_out = ReadSyncManager(*esc_, sync_manager::kMailboxOut);

	if (!mbx_out.IsEnabled()) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} else if ((mbx_out.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionWrite) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} else if ((mbx_out.settings & sync_manager_reg::kModeMask) != sync_manager_reg::kModeOneBuffer) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} else if (mbx_out.Length() < kMinMailboxSize) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} else if (mbx_out.Length() > kMaxMailboxSize) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} else if (mbx_out.Address() < kMinMbxWriteAddress) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} else if (mbx_out.Address() > kMaxMbxWriteAddress) {
		result = al_status::kInvalidMbxCfgInPreOp;
	}

	// SM1：发送邮箱
	if (result == al_status::kNoError)
	{
		const SyncManagerRegisters mbx_in = ReadSyncManager(*esc_, sync_manager::kMailboxIn);

		if (!mbx_in.IsEnabled()) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} else if ((mbx_in.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionRead) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} else if ((mbx_in.settings & sync_manager_reg::kModeMask) != sync_manager_reg::kModeOneBuffer) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} else if (mbx_in.Length() < kMinMailboxSize) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} else if (mbx_in.Length() > kMaxMailboxSize) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} else if (mbx_in.Address() < kMinMbxReadAddress) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} else if (mbx_in.Address() > kMaxMbxReadAddress) {
			result = al_status::kInvalidMbxCfgInPreOp;
		}
	}

	const PdoLayout layout = process_data_.Layout();
	const AlState   state  = state_machine_.Current();

	// SM3：输入过程数据
	if (result == al_status::kNoError && channel_count > sync_manager::kProcessIn)
	{
		const SyncManagerRegisters in_sm = ReadSyncManager(*esc_, sync_manager::kProcessIn);
		const bool     enabled = in_sm.IsEnabled();
		const uint16_t length  = in_sm.Length();

		if (enabled && length == 0) {
			result = al_status::kInvalidSmInCfg;
		} else if (enabled)
		{
			if (length != layout.tx_size || layout.tx_size == 0 || length > kMaxProcessBytes) {
				result = al_status::kInvalidSmInCfg;
			} else if ((in_sm.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionRead) {
				result = al_status::kInvalidSmInCfg;
			} else if (!((state == AlState::PreOp && in_sm.Address() >= kMinPdReadAddress && in_sm.Address() <= kMaxPdReadAddress) || (state != AlState::PreOp && in_sm.Address() == input_address_))) {
				// SSC：PREOP 检查地址落在允许区间；SAFEOP/OP 要求与进 SAFEOP 时保存的地址一致
				result = al_status::kInvalidSmInCfg;
			}
		} else if (length != 0 || layout.tx_size != 0) {
			result = al_status::kInvalidSmInCfg;
		}
	}

	// SM2：输出过程数据
	if (result == al_status::kNoError && channel_count > sync_manager::kProcessOut)
	{
		const SyncManagerRegisters out_sm = ReadSyncManager(*esc_, sync_manager::kProcessOut);
		const bool     enabled = out_sm.IsEnabled();
		const uint16_t length  = out_sm.Length();

		if (enabled && length == 0) {
			result = al_status::kInvalidSmOutCfg;
		} else if (enabled)
		{
			if (length != layout.rx_size || layout.rx_size == 0 || length > kMaxProcessBytes) {
				result = al_status::kInvalidSmOutCfg;
			} else if ((out_sm.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionWrite) {
				result = al_status::kInvalidSmOutCfg;
			} else if (!((state == AlState::PreOp && out_sm.Address() >= kMinPdWriteAddress && out_sm.Address() <= kMaxPdWriteAddress) || (state != AlState::PreOp && out_sm.Address() == output_address_))) {
				// SSC：PREOP 检查地址落在允许区间；SAFEOP/OP 要求与进 SAFEOP 时保存的地址一致
				result = al_status::kInvalidSmOutCfg;
			}
		} else if (length != 0 || layout.rx_size != 0) {
			result = al_status::kInvalidSmOutCfg;
		}
	}

	// 其余通道：读使能字节确认 SM-Change 中断
	// SSC：for (i = maxChannel; i < nMaxSyncMan; i++) { SMActivate = pSyncMan->Settings[0]; }
	// Settings[0] 是 0x0806（通道基址 +6），HPM SDK 也写明 SM_ACT 靠读 0x0806 清除
	if (result == al_status::kNoError)
	{
		for (uint8_t ch = channel_count; ch < state_machine_.SyncManagerCount(); ++ch)
		{
			uint8_t enable = 0;

			esc_->Read(static_cast<uint16_t>(reg::kSyncManagerBase + ch * kSyncManagerRegSize + reg::kSyncManagerEnableOffset), &enable, 1);
		}
	}

	return result;
}

/**
 * @brief 置位 AL Event Mask（0x0204）
 *
 * 照 SSC：读-改-写做 OR，且必须清掉 SYNC0/SYNC1 位（它们有独立中断）。
 *
 * @param mask 要置位的事件
 */
void EcatDevice::SetALEventMask(uint16_t mask) const
{
	uint8_t bytes[4] = {};

	esc_->Read(reg::kAlEventMask, bytes, sizeof(bytes));

	uint32_t value = wire::ReadLe32(bytes, 0);

	value |= mask;
	value &= ~(static_cast<uint32_t>(al_event::kSync0 | al_event::kSync1));

	wire::WriteLe32(bytes, 0, value);
	esc_->Write(reg::kAlEventMask, bytes, sizeof(bytes));
}

/**
 * @brief 清位 AL Event Mask（0x0204）
 *
 * 照 SSC：读-改-写做 AND，且必须清掉 SYNC0/SYNC1 位。
 *
 * @param mask 要保留的事件（其余清掉）
 */
void EcatDevice::ResetALEventMask(uint16_t mask) const
{
	uint8_t bytes[4] = {};

	esc_->Read(reg::kAlEventMask, bytes, sizeof(bytes));

	uint32_t value = wire::ReadLe32(bytes, 0);

	value &= mask;
	value &= ~(static_cast<uint32_t>(al_event::kSync0 | al_event::kSync1));

	wire::WriteLe32(bytes, 0, value);
	esc_->Write(reg::kAlEventMask, bytes, sizeof(bytes));
}

/**
 * @brief 把状态与状态码写进 ESC 并更新 LED
 *
 * SSC AL_StatusInd：先写 0x0134 状态码，再写完整 0x0130（状态位 + 错误位 + 设备 ID 请求），
 * 最后按 RUN/ERR 两个 LED 写 0x0138 override。
 */
void EcatDevice::Publish()
{
	const AlState current = state_machine_.Current();
	const bool     error   = state_machine_.HasError();

	uint16_t code = state_machine_.StatusCode();

	if (state_machine_.DeviceIdRequested() && !error && code == al_status::kNoError && current != AlState::Boot) {
		code = state_machine_.DeviceIdValue();
	}

	Write32(*esc_, reg::kAlStatusCode, code);

	uint8_t status = static_cast<uint8_t>(current);

	if (error) {
		status = static_cast<uint8_t>(status | al_control::kErrorAck);
	}
	if (state_machine_.DeviceIdRequested() && !error && code == state_machine_.DeviceIdValue()) {
		status = static_cast<uint8_t>(status | al_control::kDeviceIdRequest);
	}

	Write32(*esc_, reg::kAlStatus, status);

	uint8_t run_led = kLedOff;

	switch (current)
	{
		case AlState::Init:
			run_led = kLedOff;
			break;
		case AlState::PreOp:
			run_led = kLedBlinking;
			break;
		case AlState::SafeOp:
			run_led = kLedSingleFlash;
			break;
		case AlState::Op:
			run_led = kLedOn;
			break;
		case AlState::Boot:
			run_led = kLedFlickering;
			break;
	}

	uint8_t error_led = kLedOff;

	if (error)
	{
		if (code == al_status::kSyncError || code == al_status::kDcInvalidSyncCfg || code == 0x002D) {
			error_led = kLedSingleFlash;
		} else if (code == al_status::kSmWatchdog) {
			error_led = kLedDoubleFlash;
		} else {
			error_led = kLedBlinking;
		}
	}

	const uint32_t leds = static_cast<uint32_t>(run_led | kLedOverride) | (static_cast<uint32_t>(error_led | kLedOverride) << 8);

	Write32(*esc_, reg::kRunLedOverride, leds);
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
 * @brief 抓取软件计数和 ESC 实际状态，不读取会确认 AL Control 的寄存器
 * @return 当前诊断快照；硬件寄存器为顺序读取，不是硬件同时锁存
 */
DiagnosticSnapshot EcatDevice::CaptureDiagnosticSnapshot()
{
	DiagnosticSnapshot sample {};
	const unsigned key = irq_lock();

	sample.time_ms = k_uptime_get_32();
	sample.pdi = diagnostics_.PdiCount();
	sample.sm2 = diagnostics_.Sm2Count();
	sample.sm3 = diagnostics_.Sm3Count();
	sample.ticks = diagnostics_.TickCount();
	sample.age_ms = diagnostics_.Sm2AgeMs(sample.time_ms);
	sample.poll_gap_ms = diagnostics_.MaxPollGap();
	sample.state = static_cast<uint16_t>(state_machine_.Current());
	sample.code = state_machine_.StatusCode();
	sample.output = state_machine_.IsOutputRunning();
	sample.events = esc_->ReadAlEvent();

	irq_unlock(key);

	const uint16_t addresses[] = { reg::kAlStatus, reg::kAlStatusCode, reg::kDlStatus, reg::kAlEventMask };
	uint32_t values[4] = {};

	for (uint8_t i = 0; i < 4; ++i)
	{
		uint8_t bytes[4] = {};

		esc_->Read(addresses[i], bytes, sizeof(bytes));
		values[i] = wire::ReadLe32(bytes, 0);
	}
	sample.hw_state = static_cast<uint16_t>(values[0]);
	sample.hw_code = static_cast<uint16_t>(values[1]);
	sample.dl_status = static_cast<uint16_t>(values[2]);
	sample.event_mask = values[3];
	return sample;
}

}
