/**
 * @file ecat_state_machine.hpp
 * @author qingyu
 * @brief AL 状态机：转换决策、错误确认、超时判定
 * @version 0.2
 * @date 2026-11-02
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "ecat_types.hpp"

namespace ecat {

/**
 * @brief 状态机给出的一步执行动作
 *
 * 状态机只判断事实并给出下一步，执行由父层调用各域完成；
 * 状态机不再持有 mailbox / process_data / sync / sii / dictionary。
 */
enum class AlAction : uint8_t
{
	None,			// 不需要执行任何动作
	EnterPreOp,		// 打开邮箱
	EnterSafeOp,	// 备过程数据、起同步、开输入
	EnterOp,		// 开输出
	LeaveOp,		// 关输出
	LeaveSafeOp,	// 关输入、停同步
	LeavePreOp,		// 关邮箱
	BackToInit,		// 回到 INIT 的收尾
	Publish,		// 把状态与状态码写进 ESC
	ResetEventMask,	// 清 AL 事件掩码
};

/**
 * @brief 一次决策的输出
 */
struct AlStep
{
	AlAction action = AlAction::None;	// 要执行的动作
	uint16_t code   = 0;				// 该动作要用的 AL 状态码（用于 Publish）
	AlState  target = AlState::Init;	// 目标状态（仅 BackToInit / 状态发布使用）
	bool     ack    = false;			// 本次是否带错误确认
};

/**
 * @brief AL 状态机
 *
 * 只做判断：当前态、目标态、错误、pending、超时。
 * 不调用任何协议域、不读写 ESC 之外的兄弟对象。
 */
class EcatStateMachine final
{
public:
	EcatStateMachine() = default;

	EcatStateMachine(const EcatStateMachine&) = delete;
	EcatStateMachine& operator=(const EcatStateMachine&) = delete;

	void SetSyncManagerCount(uint8_t count) { sync_manager_count_ = count; }	// ESC 实际支持的 SM 通道数
	uint8_t SyncManagerCount() const { return sync_manager_count_; }			// 当前记录的 SM 通道数

	void Reset();																// 复位到 INIT
	bool HandleControl(uint8_t control, AlStep* step);							// 处理一次 AL Control 请求，输出下一步
	bool CheckTimeout(AlStep* step);											// 处理 ESM 超时，输出下一步
	void Tick();																// 1 ms 上下文：递减超时计数

	AlState  Current() 		   const { return state_; }							// 当前状态（不含错误位）
	uint16_t StatusCode() 	   const { return status_code_; }					// 当前 AL 状态码
	bool     HasError() 	   const { return error_latch_; }					// 是否有未确认的错误
	bool     IsOutputRunning() const { return output_running_; }				// 输出通道是否已开
	bool     IsInputRunning()  const { return input_running_; }					// 输入通道是否已开
	bool     HasFirstOutputs() const { return first_outputs_received_; }		// 是否已收到首帧输出

	void MarkFirstOutputs();													// PDI 收到了 SM2 输出事件
	bool CompletePendingOp(AlStep* step);										// 首帧到达后完成 SAFEOP→OP 的 pending 转换
	bool IsTransitionPending() const { return transition_pending_; }			// 是否有 pending 转换
	bool CanRecheck() const { return !transition_pending_ && !error_latch_ && state_ != AlState::Init; }	// SM_CHANGE 能否单独处理

	void SetOutputExpected(bool expected) { output_expected_ = expected; }		// 由父层告知是否存在输出过程数据
	void SetOutputRunning(bool running) { output_running_ = running; }			// 由父层回填执行结果
	void SetInputRunning(bool running) { input_running_ = running; }			// 由父层回填执行结果
	void SetState(AlState state) { state_ = state; }							// 由父层在转换完成后回填
	void SetStatusCode(uint16_t code) { status_code_ = code; }					// 由父层回填状态码
	void SetErrorLatch(bool latch) { error_latch_ = latch; }					// 由父层回填错误位
	void ClearFirstOutputs() { first_outputs_received_ = false; }				// 由父层在退态时清首帧标志
	void BeginTransition(AlState target);										// 装载 pending 与超时

	bool     DeviceIdRequested() const { return device_id_requested_; }			// 主站是否请求显式设备 ID
	uint16_t DeviceIdValue() 	 const { return device_id_value_; }				// 显式设备 ID 的值

private:
	AlStep StepFor(AlState target) const;										// 目标态对应的进入动作

	AlState  state_ 	  = AlState::Init;				// 当前 AL 状态
	uint16_t status_code_ = al_status::kNoError;		// 当前 AL 状态码
	bool     error_latch_ = false;						// AL Status 的 0x10 错误位

	bool output_expected_ = false;						// 是否存在输出过程数据（影响超时错误码）
	bool output_running_ = false;						// 已开输出（OP）
	bool input_running_  = false;						// 已开输入（SAFEOP 及以上）
	bool first_outputs_received_ = false;				// 已收到首帧输出

	bool     transition_pending_ = false;				// 在等应用或硬件完成转换
	AlState  pending_target_ 	 = AlState::Init;		// 待完成转换的目标状态
	uint16_t timeout_counter_ 	 = 0;					// 剩余毫秒数，0=未启用

	uint8_t  sync_manager_count_ = 4;					// ESC 实际支持的 SM 通道数（从 0x0004 读出）
	bool     device_id_requested_ = false;				// 主站请求显式设备 ID
	uint16_t device_id_value_ 	  = 0;					// 显式设备 ID 的值
};

}
