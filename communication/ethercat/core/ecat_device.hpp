/**
 * @file ecat_device.hpp
 * @author qingyu
 * @brief 从站协议组合根：组合各域、按上下文分发事件
 * @version 0.4
 * @date 2026-11-07
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include <zephyr/sys/atomic.h>

#include "al/ecat_state_machine.hpp"
#include "al/ecat_sync.hpp"
#include "diag/ecat_diagnostics.hpp"
#include "ecat_types.hpp"
#include "interface/ecat_esc.hpp"
#include "interface/ecat_profile.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "model/ecat_dictionary.hpp"
#include "pdo/ecat_process_data.hpp"

namespace ecat {

/**
 * @brief 从站协议组合根
 *
 * 只做编排不做协议：知道"PDI 来了先读 AL 事件、按位分发、最后确认"这个顺序，
 * 不知道状态怎么转、PDO 怎么映射、邮箱怎么重发。
 * 每个 On* 入口是一个上下文标签（ISR / 定时器 / 线程），四者能做的事完全不同。
 */
class EcatDevice final
{
public:
	using OutputSink  = void (*)(void* context, const uint8_t* data, uint16_t length);	// 主站数据接收者
	using InputSource = void (*)(void* context, uint8_t* data, uint16_t length);		// 待发数据提供者

	EcatDevice()  = default;
	~EcatDevice() = default;

	EcatDevice(const EcatDevice&) = delete;
	EcatDevice& operator=(const EcatDevice&) = delete;

	bool Prepare(EcatEsc* esc, const DeviceProfile& profile);							// 接域、开硬件、初始化到 INIT
	bool Start();																		// 发布 ready 后开启硬件事件
	bool Poll();																		// 线程上下文：AL 状态、邮箱、错误检查、链路轮询
	void Disable();																		// 停用协议栈并关闭硬件事件

	void OnPdi();																		// ISR：PDI 中断
	void OnSync0();																		// ISR：SYNC0
	void OnSync1();																		// ISR：SYNC1
	void OnTick1ms();																	// 定时器：1 ms
	void OnReset();																		// ESC 复位事件

	PdoLayout Layout()  const;															// 当前过程数据布局
	AlState   State()   const;															// 当前 AL 状态
	bool      IsReady() const { return ready_; }										// 初始化是否完成

	void SetOutputSink(OutputSink handler, void* context);								// 业务输出接收者
	void SetInputSource(InputSource handler, void* context);							// 业务输入提供者

	bool RequestFaultReport() { return diagnostics_.RequestFaultReport(); }				// 请求线程输出保存的首次故障
	void ClearFaultReport()   { diagnostics_.ClearFaultReport(); }						// 请求线程清除保存现场

	uint32_t Sm2Count() const { return diagnostics_.Sm2Count(); }						// 主站写入 SM2 累计次数
	uint32_t Sm3Count() const { return diagnostics_.Sm3Count(); }						// 主站读走 SM3 累计次数

private:
	/**
	 * @brief 读取无 AL Control 确认副作用的诊断快照
	 * @return 当前诊断快照
	 */
	DiagnosticSnapshot CaptureDiagnosticSnapshot();

	static void OnEscEvent(void* context, EcatEvent event);		// 硬件事件分发入口
	uint8_t DispatchMailbox(MailboxFrame* frame);				// 向下分发请求，返回邮箱错误码

	void HandleAlEvents(uint16_t events);						// 处理 0x0220 的各个事件位（线程上下文，完整流程）
	void HandlePdiEvents(uint16_t events);						// 处理过程数据相关事件（ISR 与线程共用）

	uint16_t ExecuteAlStep(const AlStep& step);					// 执行状态机给出的一步
	uint16_t EnterStep(const AlStep& step);						// 执行升态动作
	uint16_t Transition(AlState target, bool ack);				// 按目标态逐级退/进
	void     FinishTransition(AlState target, uint16_t result);	// 转换结果收尾

	uint16_t EnterPreOp();										// 进入 PREOP（开邮箱）
	uint16_t EnterSafeOp();										// 进入 SAFEOP
	uint16_t EnterOp();											// 进入 OP（开输出）
	uint16_t LeaveOp();											// 退出 OP
	uint16_t LeaveSafeOp();										// 退出 SAFEOP
	uint16_t LeavePreOp();										// 退出 PREOP
	void     BackToInit();										// 回到 INIT 的收尾
	void     RequestLocalState(uint16_t code, AlState target);	// 请求本地退态

	uint16_t CheckSmSettings(uint8_t channel_count) const;		// 校验 SM0~SM3 配置
	void     SetALEventMask(uint16_t mask) const;				// 置位 AL 事件掩码
	void     ResetALEventMask(uint16_t mask) const;				// 清位 AL 事件掩码
	void     Recheck();											// SM 配置变化后按当前状态重新检查
	void     CheckErrors();										// 周期性错误检查
	void     Publish();											// 把状态与状态码写进 ESC 并更新 LED

	EcatEsc*         esc_ = nullptr;							// ESC 硬件，Prepare 时绑定
	DeviceProfile    profile_ {};								// 过程数据布局

	EcatDictionary   dictionary_;								// 对象模型
	EcatStateMachine state_machine_;							// AL 状态机
	EcatSync         sync_;										// 同步与时基
	EcatProcessData  process_data_;								// 过程数据
	EcatMailbox      mailbox_;									// 邮箱传输
	EcatSdo          sdo_;										// SDO 服务
	EcatSdoInfo      sdo_info_;									// SDO Info
	EcatSiiService   sii_service_;								// SII 命令
	EcatDiagnostics  diagnostics_;								// 诊断叶子

	bool     ready_ = false;									// 初始化完成
	bool     cycle_pending_ = false;							// 有待处理的邮箱事件
	uint16_t input_address_ = 0;								// 进 SAFEOP 时验证过的 SM3 地址（SSC nEscAddrInputData）
	uint16_t output_address_ = 0;								// 进 SAFEOP 时验证过的 SM2 地址（SSC nEscAddrOutputData）
};

}
