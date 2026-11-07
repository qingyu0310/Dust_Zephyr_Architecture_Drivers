/**
 * @file ecat_diagnostics.hpp
 * @author qingyu
 * @brief 诊断叶子：观察快照、首次故障冻结、按需回放
 * @version 0.3
 * @date 2026-11-07
 *
 * 只观察不参与协议决策：不读会确认 AL Control 的寄存器，不改事件处理路径。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <zephyr/sys/atomic.h>
#include "interface/ecat_esc.hpp"

namespace ecat {

/**
 * @brief 一次观察快照
 */
struct DiagnosticSnapshot
{
	uint32_t time_ms 	 = 0;		// 采样时刻
	uint32_t pdi 		 = 0;		// PDI 入口累计
	uint32_t sm2 		 = 0;		// SM2 事件累计
	uint32_t sm3 		 = 0;		// SM3 事件累计
	uint32_t ticks 		 = 0;		// 1 ms 入口累计
	uint32_t age_ms		 = 0;		// 距最近一次 SM2 的毫秒数
	uint32_t poll_gap_ms = 0;		// 相邻 Poll 的最大间隔
	uint32_t event_mask  = 0;		// AL Event Mask
	uint16_t state 		 = 0;		// 软件 AL 状态
	uint16_t code 		 = 0;		// 软件 AL 状态码
	uint16_t hw_state  	 = 0;		// ESC 0x0130
	uint16_t hw_code 	 = 0;		// ESC 0x0134
	uint16_t dl_status   = 0;		// ESC 0x0110
	uint16_t events 	 = 0;		// ESC 0x0220
	bool     output 	 = false;	// 输出通道是否运行
};

/**
 * @brief 诊断叶子
 *
 * 拥有历史环形缓冲、首次故障现场与回放游标；协议域只负责喂计数与调用入口。
 */
class EcatDiagnostics final
{
public:
	EcatDiagnostics() = default;

	EcatDiagnostics(const EcatDiagnostics&) = delete;
	EcatDiagnostics& operator=(const EcatDiagnostics&) = delete;

	void Bind(EcatEsc* esc) { esc_ = esc; }							// 绑定硬件，仅用于抓取故障寄存器
	void OnPdi()  { atomic_inc(&pdi_count_); }				// PDI 入口计数
	void OnTick() { atomic_inc(&tick_count_); }				// 1 ms 入口计数

	/**
	 * @brief SM2 事件计数并更新最大间隔
	 * @param now_ms 当前毫秒
	 */
	void OnSm2(uint32_t now_ms);

	void OnSm3() { atomic_inc(&sm3_count_); }				// SM3 入口计数

	/**
	 * @brief 每拍调用：推进采样、故障捕获与回放
	 * @param now_ms        当前毫秒
	 * @param output_running 输出通道是否运行
	 * @param snapshot       本拍快照（由父层采集）
	 */
	void Poll(uint32_t now_ms, bool output_running, const DiagnosticSnapshot& snapshot);

	/**
	 * @brief 每秒一次的状态报告
	 * @param now_ms   当前毫秒
	 * @param snapshot 当前快照
	 * @param has_error 是否有未确认错误
	 */
	void Report(uint32_t now_ms, const DiagnosticSnapshot& snapshot, bool has_error);

	bool RequestFaultReport();																			// 请求回放保存的首次故障
	void ClearFaultReport() { atomic_set(&report_request_, 2); }						// 请求清除保存现场

	uint32_t MaxPollGap() const { return max_poll_gap_ms_; }											// 当前窗口内的最大轮询间隔
	void 	 ResetMaxPollGap()  { max_poll_gap_ms_ = 0; }												// 采样后清零

	uint32_t PdiCount()  const { return static_cast<uint32_t>(atomic_get(&pdi_count_));   }		// PDI 入口累计
	uint32_t Sm2Count()  const { return static_cast<uint32_t>(atomic_get(&sm2_count_));   }		// SM2 事件累计
	uint32_t Sm3Count()  const { return static_cast<uint32_t>(atomic_get(&sm3_count_));   }		// SM3 事件累计
	uint32_t TickCount() const { return static_cast<uint32_t>(atomic_get(&tick_count_));  }		// 1 ms 入口累计
	uint32_t MaxSm2Gap() const { return static_cast<uint32_t>(atomic_get(&max_sm2_gap_)); }		// 相邻 SM2 最大间隔

	/**
	 * @brief 距最近一次 SM2 的毫秒数
	 * @param now_ms 当前毫秒
	 * @return 从未收到 SM2 时返回 0
	 */
	uint32_t Sm2AgeMs(uint32_t now_ms) const
	{
		return (atomic_get(&sm2_count_) != 0) ? now_ms - static_cast<uint32_t>(atomic_get(&last_sm2_ms_)) : 0;
	}

private:
	static constexpr uint8_t kHistoryCount = 8;				// 每 250 ms 采样，保留约两秒历史

	EcatEsc* esc_ = nullptr;								// 硬件，仅抓故障寄存器用

	DiagnosticSnapshot history_[kHistoryCount] {};			// 历史环形缓冲
	DiagnosticSnapshot fault_history_[kHistoryCount] {};	// 故障历史副本
	DiagnosticSnapshot fault_ {};							// 首次故障现场
	uint32_t fault_registers_[8] {};						// 看门狗三项、SM2/SM3 两项、最大 SM2 间隔

	atomic_t fault_available_ = ATOMIC_INIT(0);				// 1=有可回放的故障
	atomic_t report_request_  = ATOMIC_INIT(0);				// 1=回放，2=清除

	atomic_t pdi_count_   = ATOMIC_INIT(0);					// PDI 入口累计
	atomic_t sm2_count_   = ATOMIC_INIT(0);					// SM2 事件累计
	atomic_t sm3_count_   = ATOMIC_INIT(0);					// SM3 事件累计
	atomic_t tick_count_  = ATOMIC_INIT(0);					// 1 ms 入口累计
	atomic_t last_sm2_ms_ = ATOMIC_INIT(0);					// 最近一次 SM2 的毫秒
	atomic_t max_sm2_gap_ = ATOMIC_INIT(0);					// 相邻 SM2 最大间隔

	uint32_t last_poll_ms_      = 0;						// 上次 Poll 的时刻
	uint32_t max_poll_gap_ms_   = 0;						// 本窗口内最大轮询间隔
	uint32_t next_sample_ms_    = 0;						// 下次采样时刻
	uint32_t next_dump_ms_      = 0;						// 下次回放输出时刻
	uint32_t next_report_ms_    = 0;						// 下次状态报告时刻
	uint32_t fault_id_ 		    = 0;						// 故障编号
	uint32_t additional_faults_ = 0;						// 冻结期间再次退态次数
	uint32_t previous_sm2_ 	    = 0;						// 上次报告时的 SM2 计数

	uint16_t dump_line_ 		 = 0;						// 回放行号
	uint8_t  history_next_ 		 = 0;						// 历史写指针
	uint8_t  history_size_ 		 = 0;						// 历史有效条数
	uint8_t  fault_history_size_ = 0;						// 故障历史条数

	bool poll_seen_      = false;							// 是否已 Poll 过一次
	bool output_running_ = false;							// 上一拍的输出运行标志
	bool dump_pending_   = false;							// 是否正在回放
	bool received_       = false;							// 曾在输出运行时观察到接收
	bool rx_failed_      = false;							// 上次报告处于停收状态

	void CaptureFault(const DiagnosticSnapshot& snapshot);	// 冻结历史并抓取首次故障寄存器
	void ReplayFault();										// 每 50 ms 输出一行冻结现场
};

}
