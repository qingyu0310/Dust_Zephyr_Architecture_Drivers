/**
 * @file ecat_diagnostics.cpp
 * @author qingyu
 * @brief 诊断叶子实现
 * @version 0.1
 * @date 2026-11-03
 *
 * @copyright Copyright (c) 2026
 */

#include "diag/ecat_diagnostics.hpp"

#include <zephyr/kernel.h>

#include "al/ecat_sm.hpp"
#include "spec/ecat_registers.hpp"
#include "spec/ecat_wire.hpp"
#include "log.hpp"

namespace ecat {

/**
 * @brief SM2 事件计数并更新最大间隔
 * @param now_ms 当前毫秒
 */
void EcatDiagnostics::OnSm2(uint32_t now_ms)
{
	const unsigned key = irq_lock();

	if (atomic_get(&sm2_count_) != 0)
	{
		const uint32_t gap = now_ms - static_cast<uint32_t>(atomic_get(&last_sm2_ms_));

		if (gap > static_cast<uint32_t>(atomic_get(&max_sm2_gap_))) {
			atomic_set(&max_sm2_gap_, static_cast<atomic_val_t>(gap));
		}
	}
	atomic_set(&last_sm2_ms_, static_cast<atomic_val_t>(now_ms));
	atomic_inc(&sm2_count_);

	irq_unlock(key);
}

/**
 * @brief 每拍调用：推进采样、故障捕获与回放
 * @param now_ms        当前毫秒
 * @param output_running 输出通道是否运行
 * @param snapshot       本拍快照
 */
void EcatDiagnostics::Poll(uint32_t now_ms, bool output_running, const DiagnosticSnapshot& snapshot)
{
	const atomic_val_t request = atomic_set(&report_request_, 0);

	if (request == 2)
	{
		dump_pending_ = false;
		atomic_clear(&fault_available_);
	}
	else if (request == 1 && atomic_get(&fault_available_) != 0)
	{
		dump_line_ = 0;
		next_dump_ms_ = k_uptime_get_32();
		dump_pending_ = true;
	}

	if (poll_seen_)
	{
		const uint32_t gap = now_ms - last_poll_ms_;

		if (gap > max_poll_gap_ms_) {
			max_poll_gap_ms_ = gap;
		}
	}
	last_poll_ms_ = now_ms;
	poll_seen_ = true;

	// 输出从运行转为停止：冻结首次现场，之后只累加次数
	if (output_running_ && !output_running)
	{
		if (atomic_get(&fault_available_) == 0) {
			CaptureFault(snapshot);
		} else {
			++additional_faults_;
		}
	}
	output_running_ = output_running;

	// 每 250 ms 采样一次历史
	if (!dump_pending_ && static_cast<int32_t>(now_ms - next_sample_ms_) >= 0)
	{
		history_[history_next_] = snapshot;
		max_poll_gap_ms_ = 0;
		history_next_ = static_cast<uint8_t>((history_next_ + 1U) % kHistoryCount);

		if (history_size_ < kHistoryCount) {
			++history_size_;
		}
		next_sample_ms_ = now_ms + 250U;
	}

	ReplayFault();
}

/**
 * @brief 每秒一次的状态报告
 * @param now_ms    当前毫秒
 * @param snapshot  当前快照
 * @param has_error 是否有未确认错误
 */
void EcatDiagnostics::Report(uint32_t now_ms, const DiagnosticSnapshot& snapshot, bool has_error)
{
	if (static_cast<int32_t>(now_ms - next_report_ms_) < 0) {
		return;
	}
	next_report_ms_ = now_ms + 1000U;

	const bool receiving = snapshot.output && snapshot.sm2 != previous_sm2_;

	if (receiving) {
		received_ = true;
	}

	if (has_error || snapshot.code != 0 || (received_ && !receiving))
	{
		// 每次停收只报告一次，之后静默；完整现场由 ecat fault 主动读取
		if (!rx_failed_)
		{
			CaptureFault(snapshot);
			DUST_LOG_ERR("ecat rx stopped: state=0x%02x code=0x%04x output=%u event=0x%04x", static_cast<unsigned>(snapshot.state), static_cast<unsigned>(snapshot.code), static_cast<unsigned>(snapshot.output), static_cast<unsigned>(snapshot.events));
			DUST_LOG_ERR("ecat rx stalled: age_ms=%u pdi=%u sm2=%u sm3=%u", static_cast<unsigned>(snapshot.age_ms), static_cast<unsigned>(snapshot.pdi), static_cast<unsigned>(snapshot.sm2), static_cast<unsigned>(snapshot.sm3));
		}
		rx_failed_ = true;
	}
	else if (receiving)
	{
		// 恢复正常时提示一次，稳态不刷屏
		if (rx_failed_) {
			DUST_LOG_INF("ecat rx resumed: state=0x%02x sm2=%u", static_cast<unsigned>(snapshot.state), static_cast<unsigned>(snapshot.sm2));
		}
		rx_failed_ = false;
	}

	// 尚未首次建链时保持安静，不把正常 INIT 等待误报成故障
	previous_sm2_ = snapshot.sm2;
}

/**
 * @brief 请求线程重放保存的首次故障
 * @return true=请求已提交；false=当前没有保存的故障
 */
bool EcatDiagnostics::RequestFaultReport()
{
	if (atomic_get(&fault_available_) == 0) {
		return false;
	}
	atomic_set(&report_request_, 1);
	return true;
}

/**
 * @brief 首次故障时冻结历史并保存硬件现场
 * @param snapshot 故障时刻快照
 */
void EcatDiagnostics::CaptureFault(const DiagnosticSnapshot& snapshot)
{
	// 保护首次现场，只有用户明确清除后才允许覆盖
	if (atomic_get(&fault_available_) != 0) {
		return;
	}

	fault_ = snapshot;
	fault_history_size_ = history_size_;

	const uint8_t first = static_cast<uint8_t>((history_next_ + kHistoryCount - history_size_) % kHistoryCount);

	for (uint8_t i = 0; i < fault_history_size_; ++i) {
		fault_history_[i] = history_[(first + i) % kHistoryCount];
	}

	if (esc_ == nullptr) {
		return;
	}

	const uint16_t addresses[] = { reg::kWdDivider, reg::kPdWdTime, reg::kPdWdState };

	for (uint8_t i = 0; i < 3; ++i)
	{
		uint8_t bytes[4] = {};

		esc_->Read(addresses[i], bytes, sizeof(bytes));
		fault_registers_[i] = wire::ReadLe32(bytes, 0);
	}

	const SyncManagerRegisters sm2 = ReadSyncManager(*esc_, sync_manager::kProcessOut);
	const SyncManagerRegisters sm3 = ReadSyncManager(*esc_, sync_manager::kProcessIn);

	fault_registers_[3] = sm2.address_length;
	fault_registers_[4] = sm2.settings;
	fault_registers_[5] = sm3.address_length;
	fault_registers_[6] = sm3.settings;
	fault_registers_[7] = static_cast<uint32_t>(atomic_get(&max_sm2_gap_));

	++fault_id_;
	dump_line_ = 0;
	additional_faults_ = 0;
	next_dump_ms_ = k_uptime_get_32();
	atomic_set(&fault_available_, 1);

	DUST_LOG_ERR("ecat fault saved: id=%u t=%u code=0x%04x; run 'ecat fault'", static_cast<unsigned>(fault_id_), static_cast<unsigned>(fault_.time_ms), static_cast<unsigned>(fault_.code));
}

/**
 * @brief 每 50 ms 输出一行冻结现场，避免挤满日志记录池
 */
void EcatDiagnostics::ReplayFault()
{
	if (!dump_pending_) {
		return;
	}

	const uint32_t now = k_uptime_get_32();

	if (static_cast<int32_t>(now - next_dump_ms_) < 0) {
		return;
	}
	next_dump_ms_ = now + 50U;

	const DiagnosticSnapshot& fault = fault_;
	const uint16_t line = dump_line_++;

	switch (line)
	{
		case 0:
			DUST_LOG_ERR("ecat fault id=%u t=%u age_ms=%u poll_gap_ms=%u", static_cast<unsigned>(fault_id_), static_cast<unsigned>(fault.time_ms), static_cast<unsigned>(fault.age_ms), static_cast<unsigned>(fault.poll_gap_ms));
			return;
		case 1:
			DUST_LOG_ERR("ecat fault sw: state=0x%02x code=0x%04x out=%u event=0x%04x", static_cast<unsigned>(fault.state), static_cast<unsigned>(fault.code), static_cast<unsigned>(fault.output), static_cast<unsigned>(fault.events));
			return;
		case 2:
			DUST_LOG_ERR("ecat fault hw: al=0x%04x code=0x%04x dl=0x%04x mask=0x%08x", static_cast<unsigned>(fault.hw_state), static_cast<unsigned>(fault.hw_code), static_cast<unsigned>(fault.dl_status), static_cast<unsigned>(fault.event_mask));
			return;
		case 3:
			DUST_LOG_ERR("ecat fault counts: pdi=%u sm2=%u sm3=%u tick=%u", static_cast<unsigned>(fault.pdi), static_cast<unsigned>(fault.sm2), static_cast<unsigned>(fault.sm3), static_cast<unsigned>(fault.ticks));
			return;
		case 4:
		{
			const uint32_t divider = fault_registers_[0] & 0xFFFFU;
			const uint32_t time = fault_registers_[1] & 0xFFFFU;
			const uint32_t timeout_ms = static_cast<uint32_t>(((static_cast<uint64_t>(divider) + 2U) * time + 24999U) / 25000U);

			DUST_LOG_ERR("ecat fault wd: divider=%u time=%u timeout_ms=%u raw=0x%08x", static_cast<unsigned>(divider), static_cast<unsigned>(time), static_cast<unsigned>(timeout_ms), static_cast<unsigned>(fault_registers_[2]));
			return;
		}
		case 5:
			DUST_LOG_ERR("ecat fault SM2: addr=0x%04x len=%u settings=0x%08x", static_cast<unsigned>(fault_registers_[3] & 0xFFFFU), static_cast<unsigned>(fault_registers_[3] >> 16), static_cast<unsigned>(fault_registers_[4]));
			return;
		case 6:
			DUST_LOG_ERR("ecat fault SM3: addr=0x%04x len=%u settings=0x%08x", static_cast<unsigned>(fault_registers_[5] & 0xFFFFU), static_cast<unsigned>(fault_registers_[5] >> 16), static_cast<unsigned>(fault_registers_[6]));
			return;
		case 7:
			DUST_LOG_ERR("ecat fault max_sm2_gap_ms=%u", static_cast<unsigned>(fault_registers_[7]));
			return;
		case 8:
			DUST_LOG_ERR("ecat history begin: id=%u samples=%u period_ms=250", static_cast<unsigned>(fault_id_), static_cast<unsigned>(fault_history_size_));
			return;
		default:
			break;
	}

	const uint16_t history_line = static_cast<uint16_t>(line - 9U);
	const uint8_t sample_index = static_cast<uint8_t>(history_line / 5U);

	if (sample_index >= fault_history_size_)
	{
		DUST_LOG_ERR("ecat history end: id=%u lines=%u additional_faults=%u", static_cast<unsigned>(fault_id_), static_cast<unsigned>(line + 1U), static_cast<unsigned>(additional_faults_));
		dump_pending_ = false;
		return;
	}

	const DiagnosticSnapshot& sample = fault_history_[sample_index];

	switch (history_line % 5U)
	{
		case 0:
			DUST_LOG_ERR("ecat hist t=%u state=0x%02x code=0x%04x out=%u", static_cast<unsigned>(sample.time_ms), static_cast<unsigned>(sample.state), static_cast<unsigned>(sample.code), static_cast<unsigned>(sample.output));
			break;
		case 1:
			DUST_LOG_ERR("ecat hist t=%u pdi=%u sm2=%u sm3=%u", static_cast<unsigned>(sample.time_ms), static_cast<unsigned>(sample.pdi), static_cast<unsigned>(sample.sm2), static_cast<unsigned>(sample.sm3));
			break;
		case 2:
			DUST_LOG_ERR("ecat hist t=%u age_ms=%u poll_gap_ms=%u tick=%u", static_cast<unsigned>(sample.time_ms), static_cast<unsigned>(sample.age_ms), static_cast<unsigned>(sample.poll_gap_ms), static_cast<unsigned>(sample.ticks));
			break;
		case 3:
			DUST_LOG_ERR("ecat hist t=%u al=0x%04x code=0x%04x dl=0x%04x", static_cast<unsigned>(sample.time_ms), static_cast<unsigned>(sample.hw_state), static_cast<unsigned>(sample.hw_code), static_cast<unsigned>(sample.dl_status));
			break;
		case 4:
			DUST_LOG_ERR("ecat hist t=%u event=0x%04x mask=0x%08x", static_cast<unsigned>(sample.time_ms), static_cast<unsigned>(sample.events), static_cast<unsigned>(sample.event_mask));
			break;
		default:
			break;
	}
}

}
