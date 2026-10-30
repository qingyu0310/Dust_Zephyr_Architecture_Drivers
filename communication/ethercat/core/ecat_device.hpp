/**
 * @file ecat_device.hpp
 * @author qingyu
 * @brief 从站协议组合根：组合各域、按上下文分发事件
 * @version 0.2
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "mailbox/ecat_coe.hpp"
#include "model/ecat_dictionary.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "pdo/ecat_process_data.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "al/ecat_state_machine.hpp"
#include "al/ecat_sync.hpp"
#include "ecat_types.hpp"
#include "interface/ecat_esc.hpp"

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
	using OutputSink   = void (*)(void* context, const uint8_t* data, uint16_t length);	// 主站数据接收者
	using InputSource  = void (*)(void* context, uint8_t* data, uint16_t length);		// 待发数据提供者

	EcatDevice();
	~EcatDevice();

	EcatDevice(const EcatDevice&) = delete;
	EcatDevice& operator=(const EcatDevice&) = delete;

	bool Init(EcatEsc& esc);													// 初始化协议栈与硬件
	bool Poll();																// 线程上下文：AL 状态、邮箱、错误检查、链路轮询
	void Disable();																// 停用协议栈并关闭硬件事件

	void OnPdi();																// ISR：PDI 中断
	void OnSync0();																// ISR：SYNC0
	void OnSync1();																// ISR：SYNC1
	void OnTick1ms();															// 定时器：1 ms
	void OnReset();																// ESC 复位事件

	PdoLayout Layout() const;													// 当前过程数据布局
	AlState   State() const;													// 当前 AL 状态
	bool      IsReady() const { return ready_; }								// 初始化是否完成

	void SetOutputSink(OutputSink handler,   void* context);					// 业务输出接收者
	void SetInputSource(InputSource handler, void* context);					// 业务输入提供者

private:
	EcatEsc* esc_ = nullptr;				// ESC 硬件

	EcatDictionary   dictionary_;			// 对象模型
	EcatStateMachine state_machine_;		// AL 状态机
	EcatSync         sync_;					// 同步与时基
	EcatProcessData  process_data_;			// 过程数据
	EcatMailbox      mailbox_;				// 邮箱传输
	EcatCoe          coe_;					// CoE 路由
	EcatSdo          sdo_;					// SDO 服务
	EcatSdoInfo      sdo_info_;				// SDO Info
	EcatSiiService   sii_service_;			// SII 命令

	bool    ready_ = false;					// 初始化完成
	bool    cycle_pending_  = false;		// 有待处理的邮箱事件

	static void     OnEscEvent(void* context, EcatEvent event);						// 硬件事件分发入口
	static AlState  QueryState(void* context);										// 字典的状态查询回调
	static uint64_t QueryTimestamp(void* context);									// 字典的 0x10F8 时间戳回调
	static uint8_t  DispatchMailbox(void* context, struct MailboxFrame* frame);		// 邮箱协议分发
	static uint8_t  SubmitReply(void* context, uint8_t* data, uint16_t length);		// SDO 应答提交

	void HandleAlEvents(uint16_t events);	// 处理 0x0220 的各个事件位（线程上下文，完整流程）
	void HandlePdiEvents(uint16_t events);	// 处理过程数据相关事件（ISR 与线程共用）
};

}
