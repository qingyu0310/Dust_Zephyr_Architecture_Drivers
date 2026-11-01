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
#include <zephyr/sys/atomic.h>

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

	bool RequestFaultReport();							// 请求线程输出保存的首次故障，未保存时返回 false
	void ClearFaultReport() { atomic_set(&diagnostic_report_request_, 2); }	// 请求线程清除保存现场

private:
	struct DiagnosticSnapshot
	{
		uint32_t time_ms = 0;
		uint32_t pdi = 0;
		uint32_t sm2 = 0;
		uint32_t sm3 = 0;
		uint32_t ticks = 0;
		uint32_t age_ms = 0;
		uint32_t poll_gap_ms = 0;
		uint32_t event_mask = 0;
		uint16_t state = 0;
		uint16_t code = 0;
		uint16_t hw_state = 0;
		uint16_t hw_code = 0;
		uint16_t dl_status = 0;
		uint16_t events = 0;
		bool output = false;
	};

	static constexpr uint8_t kDiagnosticHistoryCount = 8;	// 每 250 ms 采样，保留约两秒历史
	DiagnosticSnapshot diagnostic_history_[kDiagnosticHistoryCount] {};
	DiagnosticSnapshot diagnostic_fault_history_[kDiagnosticHistoryCount] {};	// 故障历史副本，不随接收恢复覆盖
	uint8_t diagnostic_fault_history_size_ = 0;
	atomic_t diagnostic_fault_available_ = ATOMIC_INIT(0);
	atomic_t diagnostic_report_request_ = ATOMIC_INIT(0);	// 1=输出保存现场，2=清除现场
	DiagnosticSnapshot diagnostic_fault_ {};				// 首次故障现场，恢复后也不覆盖待输出内容
	uint32_t diagnostic_fault_registers_[8] {};			// 看门狗三项、SM2/SM3 两项、故障时最大 SM2 间隔
	uint32_t diagnostic_last_poll_ms_ = 0;
	uint32_t diagnostic_max_poll_gap_ms_ = 0;			// 自上次历史采样以来的最大轮询间隔
	uint32_t diagnostic_next_sample_ms_ = 0;
	uint32_t diagnostic_next_dump_ms_ = 0;
	uint32_t diagnostic_fault_id_ = 0;
	uint32_t diagnostic_additional_faults_ = 0;			// 输出冻结现场期间再次退出 OP 的次数
	uint16_t diagnostic_dump_line_ = 0;
	uint8_t diagnostic_history_next_ = 0;
	uint8_t diagnostic_history_size_ = 0;
	bool diagnostic_poll_seen_ = false;
	bool diagnostic_output_running_ = false;
	bool diagnostic_dump_pending_ = false;
	atomic_t diagnostic_tick_count_ = ATOMIC_INIT(0);		// 1 ms 入口累计次数

	DiagnosticSnapshot CaptureDiagnosticSnapshot();		// 读取无 AL Control 确认副作用的诊断快照
	void CaptureDiagnosticFault();						// 冻结历史并抓取首次故障寄存器
	void ReportDiagnosticFault();						// 每 50 ms 输出一行冻结现场

	uint32_t diagnostic_previous_sm2_ = 0;			// 上次报告时的 SM2 计数
	bool diagnostic_received_ = false;				// 曾在输出运行时观察到接收
	bool diagnostic_rx_failed_ = false;				// 上次报告处于停收状态

	atomic_t diagnostic_pdi_count_ = ATOMIC_INIT(0);	// PDI 入口累计次数
	atomic_t diagnostic_sm2_count_ = ATOMIC_INIT(0);	// SM2 事件处理累计次数（含线程轮询）
	atomic_t diagnostic_last_sm2_ms_ = ATOMIC_INIT(0);	// 最近一次 SM2 事件的系统毫秒数
	atomic_t diagnostic_max_sm2_gap_ = ATOMIC_INIT(0);	// 相邻 SM2 事件最大间隔（毫秒）
	atomic_t diagnostic_sm3_count_ = ATOMIC_INIT(0);	// SM3 事件处理累计次数
	int64_t diagnostic_next_report_ = 0;				// 线程每秒输出一次诊断

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
