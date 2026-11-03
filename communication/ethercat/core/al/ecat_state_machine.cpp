/**
 * @file ecat_state_machine.cpp
 * @author qingyu
 * @brief AL 状态机实现：只做决策，执行交给父层
 * @version 0.2
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 */

#include "al/ecat_state_machine.hpp"

#include "spec/ecat_limits.hpp"
#include "log.hpp"

namespace ecat {

namespace {

/**
 * @brief 状态位的数值（用于比较）
 * @param state AL 状态
 * @return 状态位
 */
constexpr uint8_t StateBits(AlState state)
{
	return static_cast<uint8_t>(state);
}

}

/**
 * @brief 复位到 INIT
 */
void EcatStateMachine::Reset()
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
}

/**
 * @brief 处理一次 AL Control 请求
 *
 * 只做判定：是否接受、目标态是什么、要不要装载 pending。
 * 实际转换由父层调 ExecuteAlStep 完成。
 *
 * @param control 主站写入的控制字节
 * @param step    输出：下一步动作
 * @return true=处理了一个请求
 */
bool EcatStateMachine::HandleControl(uint8_t control, AlStep* step)
{
	step->action = AlAction::None;

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
			step->action = AlAction::Publish;
			return true;
	}

	*step = StepFor(target);
	step->ack = acknowledge;

	return true;
}

/**
 * @brief 目标态对应的进入动作
 * @param target 目标状态
 * @return 决策结果
 */
AlStep EcatStateMachine::StepFor(AlState target) const
{
	AlStep step {};

	step.target = target;

	switch (target)
	{
		case AlState::Init:
			step.action = AlAction::BackToInit;
			break;

		case AlState::PreOp:
			step.action = AlAction::EnterPreOp;
			break;

		case AlState::SafeOp:
			step.action = AlAction::EnterSafeOp;
			break;

		case AlState::Op:
			step.action = AlAction::EnterOp;
			break;

		default:
			step.action = AlAction::None;
			step.code = al_status::kUnknownAlControl;
			break;
	}

	return step;
}

/**
 * @brief 装载 pending 与超时
 * @param target 目标状态
 */
void EcatStateMachine::BeginTransition(AlState target)
{
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
 *
 * 只判定超时到了、该退到哪一层，退态动作由父层执行。
 *
 * @param step 输出：下一步动作
 * @return true=发生了超时
 */
bool EcatStateMachine::CheckTimeout(AlStep* step)
{
	step->action = AlAction::None;

	if (!transition_pending_ || timeout_counter_ != 0) {
		return false;
	}

	uint16_t code = al_status::kUnspecifiedError;

	switch (pending_target_)
	{
		case AlState::Op:
			// SAFEOP→OP 超时：有输出但没收到首帧 = SM 看门狗
			code = (output_expected_) ? al_status::kSmWatchdog : al_status::kUnspecifiedError;
			step->action = AlAction::LeaveOp;
			step->target = AlState::SafeOp;
			break;

		case AlState::SafeOp:
			step->action = AlAction::LeaveSafeOp;
			step->target = AlState::PreOp;
			break;

		case AlState::PreOp:
			step->action = AlAction::LeavePreOp;
			step->target = AlState::Init;
			break;

		default:
			break;
	}

	transition_pending_ = false;
	status_code_ = code;
	error_latch_ = true;
	step->code = code;

	return true;
}

/**
 * @brief PDI 收到了 SM2 输出事件
 */
void EcatStateMachine::MarkFirstOutputs()
{
	first_outputs_received_ = true;
}

/**
 * @brief 首帧到达后完成 SAFEOP→OP 的 pending 转换
 *
 * SSC AL_ControlRes：完成 OP 前要确认当前转换仍是 SAFEOP→OP、未超时、无错误。
 *
 * @param step 输出：需要执行的动作
 * @return true=有 pending 转换需要完成
 */
bool EcatStateMachine::CompletePendingOp(AlStep* step)
{
	step->action = AlAction::None;

	if (!transition_pending_ || pending_target_ != AlState::Op) {
		return false;
	}
	if (state_ != AlState::SafeOp || timeout_counter_ == 0 || error_latch_) {
		return false;
	}

	// 与 SSC 一致：首帧到达即视为 SAFEOP→OP 完成，直接置态
	transition_pending_ = false;
	status_code_ = al_status::kNoError;
	state_ = AlState::Op;
	output_running_ = true;

	DUST_LOG_INF("ecat: al_state -> 0x%02x (first outputs)", static_cast<unsigned>(state_));

	step->action = AlAction::Publish;
	step->target = AlState::Op;

	return true;
}

}
