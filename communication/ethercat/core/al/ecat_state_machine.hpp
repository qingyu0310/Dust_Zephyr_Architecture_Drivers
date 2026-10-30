/**
 * @file ecat_state_machine.hpp
 * @author qingyu
 * @brief AL 状态机：转换决策、通道开关、错误确认、超时
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "ecat_types.hpp"
#include "interface/ecat_esc.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "model/ecat_dictionary.hpp"
#include "pdo/ecat_process_data.hpp"
#include "al/ecat_sync.hpp"

namespace ecat {

/**
 * @brief AL 状态机
 *
 * 决策者但不实现被决策的动作：进 PREOP 调 mailbox.Start，
 * 进 SAFEOP 调 process_data.Configure + sync.StartSm，进 OP 置输出标志。
 * 各域检测到问题只返回结果，退态由状态机执行。
 */
class EcatStateMachine final
{
public:
	EcatStateMachine() = default;

	EcatStateMachine(const EcatStateMachine&) = delete;
	EcatStateMachine& operator=(const EcatStateMachine&) = delete;

	void Bind(EcatMailbox& mailbox, EcatProcessData& process_data, EcatSync& sync, EcatSiiService& sii);	// 绑定四个被驱动的域
	void BindDictionary(EcatDictionary& dictionary);							// 绑定字典
	void SetSyncManagerCount(uint8_t count) { sync_manager_count_ = count; }	// ESC 实际支持的 SM 通道数
	uint8_t SyncManagerCount() const { return sync_manager_count_; }			// 当前记录的 SM 通道数

	void Reset(EcatEsc& esc);													// 复位到 INIT
	bool HandleControl(EcatEsc& esc, uint8_t control);							// 处理一次 AL Control 请求
	void CheckTimeout(EcatEsc& esc);											// 处理 ESM 超时
	void Recheck(EcatEsc& esc);													// SM 配置变化后按当前状态重新检查
	void Tick();																// 1 ms 上下文：递减超时计数
	void CheckErrors(EcatEsc& esc);												// 周期性错误检查

	AlState  Current() 		   const { return state_; }							// 当前状态（不含错误位）
	uint16_t StatusCode() 	   const { return status_code_; }					// 当前 AL 状态码
	bool     HasError() 	   const { return error_latch_; }					// 是否有未确认的错误
	bool     IsOutputRunning() const { return output_running_; }				// 输出通道是否已开
	bool     IsInputRunning()  const { return input_running_; }					// 输入通道是否已开
	bool     HasFirstOutputs() const { return first_outputs_received_; }		// 是否已收到首帧输出

	void MarkFirstOutputs(EcatEsc& esc);													// PDI 收到了 SM2 输出事件
	bool IsTransitionPending() const { return transition_pending_; }				// 是否有 pending 转换
	bool CanRecheck() const { return !transition_pending_ && !error_latch_ && state_ != AlState::Init; }	// SM_CHANGE 能否单独处理

private:
	uint16_t Transition(EcatEsc& esc, AlState target, bool ack);				// 尝试一次状态转换
	uint16_t CheckSmSettings(EcatEsc& esc, uint8_t max_chan) const;				// 校验 SM0~SM3 配置
	void SetALEventMask(EcatEsc& esc, uint16_t mask) const;						// 置位 AL 事件掩码
	void ResetALEventMask(EcatEsc& esc, uint16_t mask) const;					// 清位 AL 事件掩码
	uint16_t EnterPreOp(EcatEsc& esc);											// 进入 PREOP（开邮箱）
	uint16_t EnterSafeOp(EcatEsc& esc);											// 进入 SAFEOP（备过程数据 + 起同步 + 开输入）
	uint16_t EnterOp(EcatEsc& esc);												// 进入 OP（开输出）
	uint16_t LeaveOp(EcatEsc& esc);												// 退出 OP（关输出）
	uint16_t LeaveSafeOp(EcatEsc& esc);											// 退出 SAFEOP（关输入）
	uint16_t LeavePreOp(EcatEsc& esc);											// 退出 PREOP（关邮箱）
	void     BackToInit(EcatEsc& esc);											// 回到 INIT 的收尾
	void     Publish(EcatEsc& esc);												// 把状态与状态码写进 ESC 并更新 LED
	void     RequestLocalState(EcatEsc& esc, uint16_t code, AlState target);	// 请求本地退态

	EcatMailbox*     mailbox_ 		= nullptr;			// 邮箱域
	EcatProcessData* process_data_ 	= nullptr;			// 过程数据域
	EcatSync*        sync_ 			= nullptr;			// 同步域
	EcatSiiService*  sii_ 			= nullptr;			// SII 命令服务
	EcatDictionary*  dictionary_ 	= nullptr;			// 对象字典

	AlState  state_ 	  = AlState::Init;				// 当前 AL 状态
	uint16_t status_code_ = al_status::kNoError;		// 当前 AL 状态码
	bool     error_latch_ = false;						// AL Status 的 0x10 错误位

	bool output_running_ = false;						// 已开输出（OP）
	bool input_running_  = false;						// 已开输入（SAFEOP 及以上）
	bool first_outputs_received_ = false;				// 已收到首帧输出

	bool     transition_pending_ = false;				// 在等应用或硬件完成转换
	AlState  pending_target_ 	 = AlState::Init;		// 待完成转换的目标状态
	uint16_t timeout_counter_ 	 = 0;					// 剩余毫秒数，0=未启用

	uint8_t  sync_manager_count_ = 4;					// ESC 实际支持的 SM 通道数（从 0x0004 读出）
	uint16_t input_address_  = 0;						// 进 SAFEOP 时验证过的 SM3 地址（SSC nEscAddrInputData）
	uint16_t output_address_ = 0;						// 进 SAFEOP 时验证过的 SM2 地址（SSC nEscAddrOutputData）
	bool     device_id_requested_ = false;				// 主站请求显式设备 ID
	uint16_t device_id_value_ 	  = 0;					// 显式设备 ID 的值
};

}
