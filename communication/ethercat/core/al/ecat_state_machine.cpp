/**
 * @file ecat_state_machine.cpp
 * @author qingyu
 * @brief AL 状态机实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "al/ecat_state_machine.hpp"

#include "model/ecat_dictionary.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "pdo/ecat_process_data.hpp"
#include "spec/ecat_registers.hpp"
#include "al/ecat_sm.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "al/ecat_sync.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"
#include "log.hpp"

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
 * @brief 状态位的数值（用于比较与 ESC 写入）
 * @param state AL 状态
 * @return 状态位
 */
constexpr uint8_t StateBits(AlState state)
{
	return static_cast<uint8_t>(state);
}

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
 * @brief 绑定四个被驱动的域
 * @param mailbox      邮箱域
 * @param process_data 过程数据域
 * @param sync         同步域
 * @param sii          SII 命令服务
 */
void EcatStateMachine::Bind(EcatMailbox& mailbox, EcatProcessData& process_data, EcatSync& sync, EcatSiiService& sii)
{
	mailbox_ = &mailbox;
	process_data_ = &process_data;
	sync_ = &sync;
	sii_ = &sii;
}

/**
 * @brief 绑定字典
 * @param dictionary 对象字典
 */
void EcatStateMachine::BindDictionary(EcatDictionary& dictionary)
{
	dictionary_ = &dictionary;
}

/**
 * @brief 复位到 INIT
 * @param esc ESC 硬件
 */
void EcatStateMachine::Reset(EcatEsc& esc)
{
	state_ 					= AlState::Init;
	status_code_ 			= al_status::kNoError;

	error_latch_ 			= false;

	output_running_ 		= false;
	input_running_ 			= false;

	first_outputs_received_ = false;
	transition_pending_ 	= false;

	pending_target_ 		= AlState::Init;
	timeout_counter_ 		= 0;

	device_id_requested_ 	= false;
	device_id_value_ 		= 0;

	// SSC ECAT_Init：复位时清空 AL 事件掩码
	ResetALEventMask(esc, 0);

	Publish(esc);
}

/**
 * @brief 处理一次 AL Control 请求
 * @param esc     ESC 硬件
 * @param control 主站写入的控制字节
 * @return true=处理了一个请求
 */
bool EcatStateMachine::HandleControl(EcatEsc& esc, uint8_t control)
{
	const bool    acknowledge = (control & al_control::kErrorAck) != 0;
	const uint8_t requested   = static_cast<uint8_t>(control & al_control::kStateMask);

	DUST_LOG_INF("ecat: al_control=0x%02x req=0x%02x", static_cast<unsigned>(control), static_cast<unsigned>(requested));


	device_id_requested_ = (control & al_control::kDeviceIdRequest) != 0;

	// 新请求接管旧请求：先取消上一个还没完成的转换，避免旧 pending 与超时残留
	if (transition_pending_) {
		transition_pending_ = false;
		pending_target_ = AlState::Init;
		timeout_counter_ = 0;
	}

	// 错误未确认时不允许升态，例外是请求 INIT
	if (!acknowledge && error_latch_ && requested != StateBits(AlState::Init)) {
		return false;
	}

	if (acknowledge) {
		error_latch_ = false;
	}

	AlState target = AlState::Init;

	switch (requested)
	{
		case StateBits(AlState::Init):
			target = AlState::Init;
			break;
		case StateBits(AlState::PreOp):
			target = AlState::PreOp;
			break;
		case StateBits(AlState::SafeOp):
			target = AlState::SafeOp;
			break;
		case StateBits(AlState::Op):
			target = AlState::Op;
			break;
		case StateBits(AlState::Boot):
			target = AlState::Boot;
			break;
		default:
			status_code_ = al_status::kUnknownAlControl;
			error_latch_ = true;
			Publish(esc);
			return true;
	}

	const uint16_t result = Transition(esc, target, acknowledge);

	if (result == kAlStatusInWork)
	{
		// 转换进行中：装载超时计数（SSC 已扣 10%），不置错误、不改状态
		transition_pending_ = true;
		pending_target_ = target;

		switch (target)
		{
			case AlState::PreOp:
				timeout_counter_ = kEnterPreOpTimeoutMs;
				break;
			case AlState::SafeOp:
				timeout_counter_ = kDefaultTimeoutMs;
				break;
			case AlState::Op:
				timeout_counter_ = kSafeOpToOpTimeoutMs;
				break;
			default:
				timeout_counter_ = 0;
				break;
		}
	}
	else if (result == al_status::kNoError) 
	{
		status_code_ = al_status::kNoError;
		DUST_LOG_INF("ecat: al_state -> 0x%02x", static_cast<unsigned>(state_));
	} 
	else 
	{
		status_code_ = result;
		error_latch_ = true;
		DUST_LOG_ERR("ecat: al_state 0x%02x failed code=0x%04x", static_cast<unsigned>(target), static_cast<unsigned>(result));
	}
	Publish(esc);

	return true;
}

/**
 * @brief 尝试一次状态转换
 *
 * 非法直接跳转先拦下，再按 OP→SAFEOP→PREOP→INIT 逐级退，
 * 每一步都可能失败并提前返回。
 *
 * @param esc     ESC 硬件
 * @param target  目标状态
 * @param ack     本次是否带错误确认
 * @return 0=成功；否则为 AL 状态码
 */
uint16_t EcatStateMachine::Transition(EcatEsc& esc, AlState target, bool ack)
{
	const AlState current = state_;

	// SSC：同状态请求时，若带错误确认则重新使能 SM，不算状态变化
	if (target == current) 
	{
		if (ack && (current == AlState::SafeOp || current == AlState::Op)) 
		{
			if (process_data_ != nullptr && process_data_->Layout().rx_size > 0) 
			{
				EnableSyncManagerChannel(esc, sync_manager::kProcessOut);
			} 
			else if (process_data_ != nullptr && process_data_->Layout().tx_size > 0) 
			{
				EnableSyncManagerChannel(esc, sync_manager::kProcessIn);
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

	// SSC AL_ControlInd：只有升态才校验 SM，降态（尤其回 INIT）必须先能清理
	// stateTrans 分支里 INIT_2_PREOP/OP_2_PREOP/SAFEOP_2_PREOP/PREOP_2_PREOP 只查邮箱，
	// PREOP_2_SAFEOP 及 SAFEOP/OP 内部转换查全部，其余（含任何 *_2_INIT）不查
	if (target != AlState::Init && target != AlState::Boot)
	{
		// PREOP→SAFEOP 先跑 APPL_GenerateMapping 更新 PDO 长度，再校验 SM
		if (target == AlState::SafeOp && dictionary_ != nullptr && process_data_ != nullptr)
		{
			result = process_data_->Configure(*dictionary_);

			if (result != al_status::kNoError) {
				return result;
			}
		}

		// maxChannel 是"最后一个要校验的通道索引"
		// 目标 PREOP 只校验邮箱（MAILBOX_READ+1 = 2），目标 SAFEOP/OP 校验全部
		const uint8_t max_chan = (target == AlState::PreOp) ? static_cast<uint8_t>(sync_manager::kMailboxIn + 1) : static_cast<uint8_t>(sync_manager::kProcessIn + 1);

		result = CheckSmSettings(esc, max_chan);

		if (result != al_status::kNoError) {
			return result;
		}
	}

	if (current == AlState::Op)
	{
		result = LeaveOp(esc);
		if (result != al_status::kNoError) {
			return result;
		}
	}

	if ((current == AlState::Op || current == AlState::SafeOp) && (target == AlState::Init || target == AlState::PreOp)) 
	{
		result = LeaveSafeOp(esc);
		if (result != al_status::kNoError) {
			return result;
		}
	}

	if (target == AlState::Init && (current == AlState::Op || current == AlState::SafeOp || current == AlState::PreOp)) 
	{
		result = LeavePreOp(esc);
		if (result != al_status::kNoError) {
			return result;
		}
	}

	switch (target)
	{
		case AlState::Init:
			BackToInit(esc);
			state_ = AlState::Init;
			return al_status::kNoError;

		case AlState::PreOp:
			result = EnterPreOp(esc);
			break;

		case AlState::SafeOp:
			result = EnterSafeOp(esc);
			break;

		case AlState::Op:
			result = EnterOp(esc);
			break;

		default:
			return al_status::kUnknownAlControl;
	}

	// SSC：NOERROR_INWORK 表示转换还在进行，不是失败——保持当前状态等下一拍
	if (result == kAlStatusInWork) {
		return result;
	}

	if (result == al_status::kNoError) {
		state_ = target;
	} else {
		// SSC AL_ControlInd 失败收尾：只停掉目标态对应的那一层，保持"状态与已开资源一致"
		switch (target)
		{
			case AlState::Op:
				// 进 OP 失败：输出未开，停输出并退回 SAFEOP（输入仍有效）
				LeaveOp(esc);
				state_ = AlState::SafeOp;
				break;

			case AlState::SafeOp:
				// 进 SAFEOP 失败：输入未开，退回 PREOP（邮箱仍有效）
				state_ = AlState::PreOp;
				break;

			case AlState::PreOp:
				// 进 PREOP 失败：邮箱没起来，退 INIT
				LeavePreOp(esc);
				state_ = AlState::Init;
				break;

			default:
				state_ = current;
				break;
		}
	}
	return result;
}

/**
 * @brief 进入 PREOP（开邮箱）
 * @param esc ESC 硬件
 * @return 0=成功；否则为 AL 状态码
 */
uint16_t EcatStateMachine::EnterPreOp(EcatEsc& esc)
{
	if (mailbox_ == nullptr) {
		return al_status::kUnspecifiedError;
	}

	// SSC UpdateEEPROMLoadedState：EEPROM 加载失败时拒绝进 PREOP
	{
		uint8_t config[4] = {};

		esc.Read(reg::kEepromConfig, config, sizeof(config));

		const uint32_t eeprom = wire::ReadLe32(config, 0);

		if ((eeprom & eeprom_config::kErrorCrc) != 0 || (eeprom & eeprom_config::kErrorLoad) != 0) {
			return al_status::kEeError;
		}
	}

	const uint16_t result = mailbox_->Start(esc);


	if (result != al_status::kNoError) {
		mailbox_->Stop(esc);
		return result;
	}
	return al_status::kNoError;
}

/**
 * @brief 进入 SAFEOP（备过程数据 + 起同步 + 开输入）
 * @param esc ESC 硬件
 * @return 0=成功；否则为 AL 状态码
 */
uint16_t EcatStateMachine::EnterSafeOp(EcatEsc& esc)
{
	if (process_data_ == nullptr || sync_ == nullptr) {
		return al_status::kUnspecifiedError;
	}

	// PDO 布局已在 Transition 里算过（对应 SSC 的 APPL_GenerateMapping），这里不再重复

	// 先跑可失败的校验，失败时还没动过硬件，不会留副作用
	const uint16_t prepare = sync_->PrepareSm(esc, process_data_->Layout().rx_size, process_data_->Layout().tx_size);

	if (prepare != al_status::kNoError) {
		return prepare;
	}

	const uint16_t started = sync_->StartSm(esc);

	if (started != al_status::kNoError) {
		sync_->StopSm();
		return started;
	}

	// 校验通过后才提交硬件：清过程数据、使能 SM2/SM3、写初始输入
	process_data_->Clear();
	first_outputs_received_ = false;

	if (process_data_->Layout().rx_size > 0) {
		EnableSyncManagerChannel(esc, sync_manager::kProcessOut);
	}
	if (process_data_->Layout().tx_size > 0) {
		EnableSyncManagerChannel(esc, sync_manager::kProcessIn);
	}

	// SSC StartInputHandler：保存验证过的 SM2/SM3 地址，SAFEOP/OP 期间用于一致性比对
	{
		const SyncManagerRegisters out_registers = ReadSyncManager(esc, sync_manager::kProcessOut);
		const SyncManagerRegisters in_registers  = ReadSyncManager(esc, sync_manager::kProcessIn);
		const SyncManagerRegisters mbx_out       = ReadSyncManager(esc, sync_manager::kMailboxOut);
		const SyncManagerRegisters mbx_in        = ReadSyncManager(esc, sync_manager::kMailboxIn);

		output_address_ = out_registers.Address();
		input_address_  = in_registers.Address();

		// SSC：过程数据区不得与邮箱区重叠，SM2 与 SM3 之间也不得重叠
		const uint16_t overlap = process_data_->ValidateAreas(out_registers.Address(), in_registers.Address(), mbx_out.Address(), mbx_out.Length(), mbx_in.Address(), mbx_in.Length());

		if (overlap != al_status::kNoError) {
			DisableSyncManagerChannel(esc, sync_manager::kProcessOut);
			DisableSyncManagerChannel(esc, sync_manager::kProcessIn);
			sync_->StopSm();
			return overlap;
		}
	}

	{
		const SyncManagerRegisters in_registers = ReadSyncManager(esc, sync_manager::kProcessIn);

		process_data_->PublishInputs(esc, in_registers.Address());
	}

	// SSC SetALEventMask：读-改-写，且必须清掉 SYNC0/SYNC1（它们有独立中断）
	uint16_t event_mask = al_event::kAlControl | al_event::kEepromCommand;

	if (process_data_->Layout().rx_size > 0) {
		event_mask |= al_event::kProcessOutput;
	} else if (process_data_->Layout().tx_size > 0) {
		event_mask |= al_event::kProcessInput;
	}

	SetALEventMask(esc, event_mask);

	input_running_ = true;
	return al_status::kNoError;
}

/**
 * @brief 进入 OP（开输出）
 *
 * SAFEOP→OP 的门槛：必须先收到首帧输出。
 *
 * @param esc ESC 硬件
 * @return 0=成功；否则为 AL 状态码
 */
uint16_t EcatStateMachine::EnterOp(EcatEsc& esc)
{
	// SSC SAFEOP_2_OP：没收到首帧输出时保持 pending，等下一拍再完成，不是拒绝
	if (process_data_ != nullptr && process_data_->Layout().rx_size > 0 && !first_outputs_received_) {
		return kAlStatusInWork;
	}

	// SSC SAFEOP_2_OP：使能输出同步管理器
	if (process_data_ != nullptr && process_data_->Layout().rx_size > 0) {
		EnableSyncManagerChannel(esc, sync_manager::kProcessOut);
	}

	output_running_ = true;
	return al_status::kNoError;
}

/**
 * @brief 退出 OP（关输出）
 * @param esc ESC 硬件
 * @return 0=成功
 */
uint16_t EcatStateMachine::LeaveOp(EcatEsc& esc)
{
	(void)esc;

	output_running_ = false;
	first_outputs_received_ = false;
	return al_status::kNoError;
}

/**
 * @brief 退出 SAFEOP（关输入）
 * @param esc ESC 硬件
 * @return 0=成功
 */
uint16_t EcatStateMachine::LeaveSafeOp(EcatEsc& esc)
{
	// SSC StopInputHandler：禁用 SM2/SM3
	if (process_data_ != nullptr) {
		if (process_data_->Layout().rx_size > 0) {
			DisableSyncManagerChannel(esc, sync_manager::kProcessOut);
		}
		if (process_data_->Layout().tx_size > 0) {
			DisableSyncManagerChannel(esc, sync_manager::kProcessIn);
		}
	}

	// 清掉 SYNC0/SYNC1/过程数据事件位
	ResetALEventMask(esc, static_cast<uint16_t>(~(al_event::kSync0 | al_event::kSync1 | al_event::kProcessOutput | al_event::kProcessInput)));

	// 复位同步域全部状态（DC 标志、看门狗计数、序列有效性）
	if (sync_ != nullptr) {
		sync_->StopSm();
		sync_->ResetUserSyncType();
	}

	first_outputs_received_ = false;
	input_running_ = false;
	return al_status::kNoError;
}

/**
 * @brief 退出 PREOP（关邮箱）
 * @param esc ESC 硬件
 * @return 0=成功
 */
uint16_t EcatStateMachine::LeavePreOp(EcatEsc& esc)
{
	if (mailbox_ != nullptr) {
		mailbox_->Stop(esc);
	}
	return al_status::kNoError;
}

/**
 * @brief 回到 INIT 的收尾
 * @param esc ESC 硬件
 */
void EcatStateMachine::BackToInit(EcatEsc& esc)
{
	(void)esc;

	if (sync_ != nullptr) {
		sync_->StopSm();
	}
	sync_->ResetUserSyncType();
	output_running_ = false;
	input_running_ = false;
	first_outputs_received_ = false;
}

/**
 * @brief 把当前状态与状态码写进 ESC 并更新 LED
 * @param esc ESC 硬件
 */
void EcatStateMachine::Publish(EcatEsc& esc)
{
	uint16_t code = status_code_;

	if (device_id_requested_ && !error_latch_ && code == al_status::kNoError && state_ != AlState::Boot) {
		code = device_id_value_;
	}

	Write32(esc, reg::kAlStatusCode, code);

	uint8_t status = static_cast<uint8_t>(StateBits(state_));

	if (error_latch_) {
		status = static_cast<uint8_t>(status | al_control::kErrorAck);
	}
	if (device_id_requested_ && !error_latch_ && code == device_id_value_) {
		status = static_cast<uint8_t>(status | al_control::kDeviceIdRequest);
	}

	Write32(esc, reg::kAlStatus, status);

	uint8_t run_led = kLedOff;

	switch (state_)
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

	if (error_latch_) {
		if (code == al_status::kSyncError || code == al_status::kDcInvalidSyncCfg || code == 0x002D) {
			error_led = kLedSingleFlash;
		} else if (code == al_status::kSmWatchdog) {
			error_led = kLedDoubleFlash;
		} else {
			error_led = kLedBlinking;
		}
	}

	const uint32_t leds = static_cast<uint32_t>(run_led | kLedOverride) | (static_cast<uint32_t>(error_led | kLedOverride) << 8);

	Write32(esc, reg::kRunLedOverride, leds);
}

/**
 * @brief PDI 收到 SM2 输出事件
 *
 * SSC：首帧到达后若 SAFEOP→OP 还在 pending，立刻完成转换。
 */
void EcatStateMachine::MarkFirstOutputs(EcatEsc& esc)
{
	first_outputs_received_ = true;

	// SSC AL_ControlRes：完成 OP 前要确认当前转换仍是 SAFEOP→OP、未超时、无错误
	if (!transition_pending_ || pending_target_ != AlState::Op) {
		return;
	}
	if (state_ != AlState::SafeOp || timeout_counter_ == 0 || error_latch_) {
		return;
	}

	transition_pending_ = false;
	status_code_ = al_status::kNoError;
	state_ = AlState::Op;
	output_running_ = true;

	// 状态变化必须写回 ESC，否则主站看不到转换完成
	Publish(esc);
	DUST_LOG_INF("ecat: al_state -> 0x%02x (first outputs)", static_cast<unsigned>(state_));
}

/**
 * @brief 1 ms 上下文：递减超时计数
 */
void EcatStateMachine::Tick()
{
	if (timeout_counter_ > 0) {
		--timeout_counter_;
	}
}

/**
 * @brief 处理 ESM 超时
 * @param esc ESC 硬件
 */
void EcatStateMachine::CheckTimeout(EcatEsc& esc)
{
	if (!transition_pending_ || timeout_counter_ != 0) {
		return;
	}

	// SSC AL_ControlRes：按 pending 的转换类型分别收尾并给出具体错误码
	uint16_t code = al_status::kUnspecifiedError;

	switch (pending_target_)
	{
		case AlState::Op:
			// SAFEOP→OP 超时：有输出但没收到首帧 = SM 看门狗
			if (process_data_ != nullptr && process_data_->Layout().rx_size > 0) {
				code = al_status::kSmWatchdog;
			}
			LeaveOp(esc);
			break;

		case AlState::SafeOp:
			code = al_status::kUnspecifiedError;
			LeaveSafeOp(esc);
			break;

		case AlState::PreOp:
			code = al_status::kUnspecifiedError;
			LeavePreOp(esc);
			break;

		default:
			break;
	}

	transition_pending_ = false;
	status_code_ = code;
	error_latch_ = true;
	Publish(esc);
}

/**
 * @brief 置位 AL Event Mask（0x0204）
 *
 * 照 SSC：读-改-写做 OR，且必须清掉 SYNC0/SYNC1 位（它们有独立中断）。
 *
 * @param esc  ESC 硬件
 * @param mask 要置位的事件
 */
void EcatStateMachine::SetALEventMask(EcatEsc& esc, uint16_t mask) const
{
	uint8_t bytes[4] = {};

	esc.Read(reg::kAlEventMask, bytes, sizeof(bytes));

	uint32_t value = wire::ReadLe32(bytes, 0);

	value |= mask;
	value &= ~(static_cast<uint32_t>(al_event::kSync0 | al_event::kSync1));

	wire::WriteLe32(bytes, 0, value);
	esc.Write(reg::kAlEventMask, bytes, sizeof(bytes));
}

/**
 * @brief 清位 AL Event Mask（0x0204）
 *
 * 照 SSC：读-改-写做 AND，且必须清掉 SYNC0/SYNC1 位。
 *
 * @param esc  ESC 硬件
 * @param mask 要保留的事件（其余清掉）
 */
void EcatStateMachine::ResetALEventMask(EcatEsc& esc, uint16_t mask) const
{
	uint8_t bytes[4] = {};

	esc.Read(reg::kAlEventMask, bytes, sizeof(bytes));

	uint32_t value = wire::ReadLe32(bytes, 0);

	value &= mask;
	value &= ~(static_cast<uint32_t>(al_event::kSync0 | al_event::kSync1));

	wire::WriteLe32(bytes, 0, value);
	esc.Write(reg::kAlEventMask, bytes, sizeof(bytes));
}

/**
 * @brief 校验 SM0~SM3 的配置
 *
 * 照 SSC CheckSmSettings：邮箱区校验使能位/方向/缓冲模式/长度/地址，
 * 过程数据区校验长度必须与 PDO 布局一致、方向必须正确。
 *
 * @param esc       ESC 硬件
 * @param max_chan  校验到哪个通道（含）
 * @return 0=通过，否则为 AL 状态码
 */
uint16_t EcatStateMachine::CheckSmSettings(EcatEsc& esc, uint8_t max_chan) const
{
	uint16_t result = al_status::kNoError;

	// ---- SM0：接收邮箱 ----
	const SyncManagerRegisters mbx_out = ReadSyncManager(esc, sync_manager::kMailboxOut);

	if (!mbx_out.IsEnabled()) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} 
	else if ((mbx_out.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionWrite) 	{
		result = al_status::kInvalidMbxCfgInPreOp;
	} 
	else if ((mbx_out.settings & sync_manager_reg::kModeMask) != sync_manager_reg::kModeOneBuffer) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} 
	else if (mbx_out.Length() < kMinMailboxSize) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} 
	else if (mbx_out.Length() > kMaxMailboxSize) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} 
	else if (mbx_out.Address() < kMinMbxWriteAddress) {
		result = al_status::kInvalidMbxCfgInPreOp;
	} 
	else if (mbx_out.Address() > kMaxMbxWriteAddress) {
		result = al_status::kInvalidMbxCfgInPreOp;
	}

	// ---- SM1：发送邮箱 ----
	if (result == al_status::kNoError) 
	{
		const SyncManagerRegisters mbx_in = ReadSyncManager(esc, sync_manager::kMailboxIn);

		if (!mbx_in.IsEnabled()) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} 
		else if ((mbx_in.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionRead) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} 
		else if ((mbx_in.settings & sync_manager_reg::kModeMask) != sync_manager_reg::kModeOneBuffer) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} 
		else if (mbx_in.Length() < kMinMailboxSize) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} 
		else if (mbx_in.Length() > kMaxMailboxSize) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} 
		else if (mbx_in.Address() < kMinMbxReadAddress) {
			result = al_status::kInvalidMbxCfgInPreOp;
		} 
		else if (mbx_in.Address() > kMaxMbxReadAddress) {
			result = al_status::kInvalidMbxCfgInPreOp;
		}
	}

	const PdoLayout layout = (process_data_ != nullptr) ? process_data_->Layout() : PdoLayout {};

	// ---- SM3：输入过程数据 ----
	if (result == al_status::kNoError && max_chan > sync_manager::kProcessIn) 
	{
		const SyncManagerRegisters in_sm = ReadSyncManager(esc, sync_manager::kProcessIn);
		const bool enabled = in_sm.IsEnabled();
		const uint16_t length = in_sm.Length();

		if (enabled && length == 0) 
		{
			result = al_status::kInvalidSmInCfg;
		} 
		else if (enabled) 
		{
			if (length != layout.tx_size || layout.tx_size == 0 || length > kMaxProcessInputSize) 
			{
				result = al_status::kInvalidSmInCfg;
			} 
			else if ((in_sm.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionRead) 
			{
				result = al_status::kInvalidSmInCfg;
			} 
			else if (!((state_ == AlState::PreOp && in_sm.Address() >= kMinPdReadAddress && in_sm.Address() <= kMaxPdReadAddress) || (state_ != AlState::PreOp && in_sm.Address() == input_address_)))
			{
				// SSC：PREOP 检查地址落在允许区间；SAFEOP/OP 要求与进 SAFEOP 时保存的地址一致
				result = al_status::kInvalidSmInCfg;
			}
		} 
		else if (length != 0 || layout.tx_size != 0) 
		{
			result = al_status::kInvalidSmInCfg;
		}
	}

	// ---- SM2：输出过程数据 ----
	if (result == al_status::kNoError && max_chan > sync_manager::kProcessOut) 
	{
		const SyncManagerRegisters out_sm = ReadSyncManager(esc, sync_manager::kProcessOut);
		const bool 	   enabled = out_sm.IsEnabled();
		const uint16_t length  = out_sm.Length();

		if (enabled && length == 0) 
		{
			result = al_status::kInvalidSmOutCfg;
		} 
		else if (enabled) 
		{
			if (length != layout.rx_size || layout.rx_size == 0 || length > kMaxProcessOutputSize) {
				result = al_status::kInvalidSmOutCfg;
			} 
			else if ((out_sm.settings & sync_manager_reg::kDirectionMask) != sync_manager_reg::kDirectionWrite) {
				result = al_status::kInvalidSmOutCfg;
			} 
			else if (!((state_ == AlState::PreOp && out_sm.Address() >= kMinPdWriteAddress && out_sm.Address() <= kMaxPdWriteAddress) || (state_ != AlState::PreOp && out_sm.Address() == output_address_))) {
				// SSC：PREOP 检查地址落在允许区间；SAFEOP/OP 要求与进 SAFEOP 时保存的地址一致
				result = al_status::kInvalidSmOutCfg;
			}
		} 
		else if (length != 0 || layout.rx_size != 0) {
			result = al_status::kInvalidSmOutCfg;
		}
	}

	// ---- 其余通道：读使能字节确认 SM-Change 中断 ----
	// SSC：for (i = maxChannel; i < nMaxSyncMan; i++) { SMActivate = pSyncMan->Settings[0]; }
	// Settings[0] 是 0x0806（通道基址 +6），HPM SDK 也写明 SM_ACT 靠读 0x0806 清除
	if (result == al_status::kNoError)
	{
		for (uint8_t ch = max_chan; ch < sync_manager_count_; ++ch)
		{
			uint8_t enable = 0;

			esc.Read(static_cast<uint16_t>(reg::kSyncManagerBase + ch * kSyncManagerRegSize + reg::kSyncManagerEnableOffset), &enable, 1);
		}
	}

	return result;
}

/**
 * @brief SM 配置变化后按当前状态重新检查
 *
 * 照 SSC ECAT_Main：SM_CHANGE 事件置位时以当前状态再走一次 AL_ControlInd。
 * 同状态分支只做两件事——校验 SM 配置、失败则退态；不重跑进入动作
 * （重跑会把主站刚写的 SM 配置冲掉，导致 SM_CHANGE 无限触发）。
 *
 * @param esc ESC 硬件
 */
void EcatStateMachine::Recheck(EcatEsc& esc)
{
	// SSC ECAT_Main 的 SM_CHANGE 分支只调 AL_ControlInd，不跳过 INIT
	// 这里同样不提前返回，靠 CheckSmSettings 的确认循环清掉 SM_ACT
	const uint8_t  max_chan = (state_ == AlState::PreOp) ? static_cast<uint8_t>(sync_manager::kMailboxIn + 1) : static_cast<uint8_t>(sync_manager::kProcessIn + 1);
	const uint16_t result = CheckSmSettings(esc, max_chan);

	if (result != al_status::kNoError) 
	{
		// 配置非法：按当前状态逐级回退
		if (state_ == AlState::Op) 
		{
			LeaveOp(esc);
			LeaveSafeOp(esc);
			state_ = AlState::SafeOp;
		} 
		else if (state_ == AlState::SafeOp) 
		{
			LeaveSafeOp(esc);
			state_ = AlState::PreOp;
		}

		status_code_ = result;
		error_latch_ = true;
		Publish(esc);
		return;
	}

	// 配置合法：只读检查，不写 SM 寄存器
	// （写 SM 会再次触发 SM_CHANGE 事件，形成死循环）
	// SSC 只在主站带错误确认时才重新使能 SM，那条路径在 Transition 的同态分支里
}

/**
 * @brief 周期性错误检查
 *
 * 过程数据看门狗、DC 同步失败都会在这里变成"要求退到 SAFEOP"。
 *
 * @param esc ESC 硬件
 */
void EcatStateMachine::CheckErrors(EcatEsc& esc)
{
	// SSC CheckIfEcatError：只在主站配置了看门狗时间时才检查（EcatWdValue != 0）
	if (sync_ != nullptr && sync_->HasProcessWatchdog() && process_data_ != nullptr && process_data_->Layout().rx_size > 0)
	{
		const uint32_t watchdog = Read32(esc, reg::kPdWdState);

		if ((watchdog & kPdWdTriggerMask) == 0) {
			// 输出正在运行：退到 SAFEOP 并报 SM 看门狗
			if (output_running_) {
				RequestLocalState(esc, al_status::kSmWatchdog, AlState::SafeOp);
				return;
			}

			// 未运行：只清首帧标志
			first_outputs_received_ = false;
		}
	}

	// DC 看门狗只在 1 ms 路径推进（OnTick1ms 里调 sync_.Tick），这里只读结果
	if (sync_ != nullptr && sync_->IsDcActive() && output_running_)
	{
		if (sync_->IsSyncSequenceValid() == false) {
			RequestLocalState(esc, al_status::kSyncError, AlState::SafeOp);
		}
	}
}

/**
 * @brief 请求本地退态
 * @param esc    ESC 硬件
 * @param code   AL 状态码
 * @param target 目标状态
 */
void EcatStateMachine::RequestLocalState(EcatEsc& esc, uint16_t code, AlState target)
{
	if (state_ == AlState::Op && target != AlState::Op) {
		LeaveOp(esc);
	}

	state_ = target;
	status_code_ = code;
	error_latch_ = true;
	Publish(esc);
}

}
