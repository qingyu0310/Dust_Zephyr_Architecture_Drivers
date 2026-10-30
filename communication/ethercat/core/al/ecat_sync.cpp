/**
 * @file ecat_sync.cpp
 * @author qingyu
 * @brief 同步与时基实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "al/ecat_sync.hpp"

#include "spec/ecat_limits.hpp"
#include "spec/ecat_registers.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"

namespace ecat {

namespace {

constexpr uint16_t kSyncTypeFreeRunSupp   = 0x0001;	// 支持自由运行
constexpr uint16_t kSyncTypeSynchronSupp  = 0x0002;	// 支持 SM 同步
constexpr uint16_t kSyncTypeDcSync0Supp   = 0x0004;	// 支持 SYNC0
constexpr uint16_t kSyncTypeDcSync1Supp   = 0x0008;	// 支持 SYNC1
constexpr uint16_t kSyncTypeSubcycleSupp  = 0x0010;	// 支持 SYNC0 下的子周期
constexpr uint16_t kSyncTypeTimesVariable = 0x4000;	// 支持动态周期

/**
 * @brief 读 32 位寄存器
 * @param esc     ESC 硬件
 * @param address 寄存器偏移
 * @return 寄存器值
 */
uint32_t ReadRegister32(EcatEsc& esc, uint16_t address)
{
	uint8_t bytes[4] = {};

	esc.Read(address, bytes, sizeof(bytes));
	return wire::ReadLe32(bytes, 0);
}

}

/**
 * @brief 绑定 0x1C32 / 0x1C33 的数据槽
 * @param out SM2 参数（0x1C32）
 * @param in  SM3 参数（0x1C33）
 */
void EcatSync::BindParameters(ObjectData::SyncParameters& out, ObjectData::SyncParameters& in)
{
	out_ = &out;
	in_ = &in;
	bound_ = true;
}

/**
 * @brief 装载 0x1C32/0x1C33 的初值
 */
void EcatSync::ResetParameters()
{
	if (!bound_) {
		return;
	}

	out_->sub_count = 32;
	out_->sync_type = static_cast<uint16_t>(SyncType::FreeRun);
	out_->cycle_time = 0;
	out_->sync_types_supported = kSyncTypeFreeRunSupp | kSyncTypeTimesVariable | kSyncTypeSynchronSupp | kSyncTypeDcSync0Supp | kSyncTypeDcSync1Supp | kSyncTypeSubcycleSupp;
	out_->min_cycle_time = kMinProcessCycleTimeNs;
	out_->calc_and_copy_time = kOutputCalcCopyTimeNs;
	out_->get_cycle_time = 0;
	out_->delay_time = kOutputDelayTimeNs;
	out_->sm_event_missed = 0;
	out_->cycle_exceeded = 0;
	out_->sync_error = 0;

	in_->sub_count = 32;
	in_->sync_type = static_cast<uint16_t>(SyncType::FreeRun);
	in_->cycle_time = out_->cycle_time;
	in_->sync_types_supported = out_->sync_types_supported;
	in_->min_cycle_time = kMinProcessCycleTimeNs;
	in_->calc_and_copy_time = kInputCalcCopyTimeNs;
	in_->get_cycle_time = 0;
	in_->delay_time = kInputDelayTimeNs;
	in_->sm_event_missed = 0;
	in_->cycle_exceeded = 0;
	in_->sync_error = 0;

	sync_set_by_user_ = false;
	dc_sync_active_ = false;
	dc_running_ = false;
	sm_running_ = false;
	sm_sync0_counter_ = 0;
	sm_sync0_value_ = 0;
	sync0_wd_counter_ = 0;
	sync0_wd_value_ = 0;
	sync1_wd_counter_ = 0;
	sync1_wd_value_ = 0;
	latch_input_value_ = 0;
	latch_input_counter_ = 0;
	sm_sequence_valid_ = true;
	timestamp_ = 0;
	last_dc32_value_ = 0;
	dc_overrun_counter_ = 0;
}

/**
 * @brief 转换到 PREOP 前准备同步参数
 *
 * 读 DC 控制寄存器，校验主站配置的同步方式是否与从站能力相符。
 *
 * @param esc ESC 硬件
 * @return 0=成功，否则为 AL 状态码
 */
uint16_t EcatSync::PrepareSm(EcatEsc& esc, uint16_t output_size, uint16_t input_size)
{
	if (!bound_) {
		return al_status::kNoError;
	}

	out_size_ = output_size;
	in_size_  = input_size;

	// SSC StartInputHandler：读 0x0420 看门狗时间与 0x0400 分频，算出实际超时（ms）
	{
		const uint16_t wd = static_cast<uint16_t>(ReadRegister32(esc, reg::kPdWdTime) & 0xFFFFU);

		if (output_size > 0 && wd != 0) {
			const uint16_t divider = static_cast<uint16_t>(ReadRegister32(esc, reg::kWdDivider) & 0xFFFFU);

			if (divider != 0) {
				// ESC 的 0x0400 存的是分频-2，这里补回；再按 25 MHz 折算成 ms 并向上取整
				uint32_t d = static_cast<uint32_t>(divider) + 2U;

				d *= wd;
				d += 24999U;
				d /= 25000U;
				process_wd_value_ = static_cast<uint16_t>(d);
			} else {
				process_wd_value_ = 0;
			}
		} else {
			process_wd_value_ = 0;
		}
	}

	const uint32_t control = ReadRegister32(esc, reg::kDcUnitControl);
	const bool unit_active = (control & (dc_control::kSyncUnitActive | dc_control::kSyncUnitAutoActive)) != 0;

	if (unit_active) {
		const bool sync0 = (control & dc_control::kSync0Active) != 0;
		const bool sync1 = (control & dc_control::kSync1Active) != 0;

		// DC 单元激活时至少要产生一路同步信号，且 SYNC1 只能与 SYNC0 一起用
		if (!sync0 && !sync1) {
			return al_status::kDcInvalidSyncCfg;
		}
		if (!sync0 && sync1) {
			return al_status::kDcInvalidSyncCfg;
		}

		const uint32_t sync0_cycle = ReadRegister32(esc, reg::kDcSync0CycleTime);

		if (sync0_cycle != 0 && (sync0_cycle < out_->min_cycle_time || sync0_cycle > kMaxProcessCycleTimeNs)) {
			return al_status::kDcSync0CycleTime;
		}
	}

	// 主站显式写过 0x1C3x:01 就以它为准，否则用 DC 寄存器推导
	if (sync_set_by_user_) {
		const uint16_t check_out = ValidateSyncType(static_cast<SyncType>(out_->sync_type), out_->sync_types_supported);

		if (check_out != al_status::kNoError) {
			return check_out;
		}

		const uint16_t check_in = ValidateSyncType(static_cast<SyncType>(in_->sync_type), in_->sync_types_supported);

		if (check_in != al_status::kNoError) {
			return check_in;
		}
	} else {
		// SSC StartInputHandler：DC 未激活时按有无过程数据选 SM 同步类型
		uint16_t sync_out = static_cast<uint16_t>(SyncType::FreeRun);
		uint16_t sync_in = static_cast<uint16_t>(SyncType::FreeRun);

		if (!unit_active) {
			// DC 未激活：有输出走 SM 同步，有输入走 SM2 同步
			sync_out = (output_size > 0) ? static_cast<uint16_t>(SyncType::SmSynchron) : static_cast<uint16_t>(SyncType::FreeRun);
			sync_in  = (input_size > 0) ? static_cast<uint16_t>((output_size > 0) ? SyncType::Sm2Synchron : SyncType::SmSynchron) : static_cast<uint16_t>(SyncType::FreeRun);
		} else {
			const bool sync1 = (control & dc_control::kSync1Active) != 0;
			const uint16_t dc_type = static_cast<uint16_t>(sync1 ? SyncType::DcSync1 : SyncType::DcSync0);

			sync_out = (output_size > 0) ? dc_type : static_cast<uint16_t>(SyncType::FreeRun);
			sync_in  = (input_size > 0) ? dc_type : static_cast<uint16_t>(SyncType::FreeRun);
		}

		out_->sync_type = sync_out;
		in_->sync_type = sync_in;
	}

	dc_sync_active_ = (out_->sync_type == static_cast<uint16_t>(SyncType::DcSync0)) || (out_->sync_type == static_cast<uint16_t>(SyncType::DcSync1));

	return al_status::kNoError;
}

/**
 * @brief 进入 SAFEOP 时启动同步监控
 * @param esc ESC 硬件
 * @return 0=成功，否则为 AL 状态码
 */
uint16_t EcatSync::StartSm(EcatEsc& esc)
{
	if (!bound_) {
		return al_status::kNoError;
	}

	sm_sync0_counter_ = 0;
	latch_input_counter_ = 0;
	sync0_wd_counter_ = 0;
	sync1_wd_counter_ = 0;
	dc_running_ = false;
	sm_running_ = false;
	sm_sequence_valid_ = true;
	sm_sync0_value_ = 0;
	sync0_wd_value_ = 0;
	sync1_wd_value_ = 0;

	// 输入锁存点：DC 同步在一个 SYNC0 周期内提交，SM 同步由 SM2 事件触发
	latch_input_value_ = dc_sync_active_ ? 1 : 0;

	// SSC StartInputHandler：按 DC 寄存器装载看门狗阈值与 Sync0 计数
	if (dc_sync_active_)
	{
		const uint32_t control = ReadRegister32(esc, reg::kDcUnitControl);
		const uint32_t sync0_cycle = ReadRegister32(esc, reg::kDcSync0CycleTime);
		const uint32_t sync1_cycle = ReadRegister32(esc, reg::kDcSync1CycleTime);

		// Sync0 看门狗：周期 > 500us 时取 2 个周期，否则固定 1 ms
		if ((control & dc_control::kSync0Active) != 0 && sync0_cycle != 0)
		{
			const uint32_t cycles = sync0_cycle / 100000U;

			sync0_wd_value_ = (cycles < 5U) ? 1U : static_cast<uint16_t>((cycles * 2U) / 10U);
		}

		// Sync1 看门狗
		if ((control & dc_control::kSync1Active) != 0 && sync0_cycle != 0)
		{
			if (sync1_cycle < sync0_cycle) {
				sync1_wd_value_ = sync0_wd_value_;
			} else {
				const uint32_t cycles = (sync1_cycle + sync0_cycle) / 100000U;

				sync1_wd_value_ = (cycles < 5U) ? 1U : static_cast<uint16_t>((cycles * 2U) / 10U);
				sync1_wd_value_ = static_cast<uint16_t>(sync1_wd_value_ + sync0_wd_value_ / 2U);
			}
		}

		// 子周期：Sync1 周期大于 Sync0 时，一个 SM 周期里有多个 SYNC0
		if ((control & dc_control::kSync0Active) != 0 && (control & dc_control::kSync1Active) != 0 && sync1_cycle >= sync0_cycle && sync0_cycle != 0)
		{
			const uint32_t total = sync1_cycle + sync0_cycle;

			sm_sync0_value_ = static_cast<uint16_t>(total / sync0_cycle);

			if ((total % sync0_cycle) == 0) {
				++sm_sync0_value_;
			}

			latch_input_value_ = static_cast<uint16_t>(total / sync0_cycle);

			if ((total % sync0_cycle) > 0) {
				++latch_input_value_;
			}
		}
		else if (out_->sync_type == static_cast<uint16_t>(SyncType::DcSync0))
		{
			sm_sync0_value_ = 1;
			latch_input_value_ = 1;
		}
	}

	return al_status::kNoError;
}

/**
 * @brief 退出 SAFEOP 时停止同步监控
 */
void EcatSync::StopSm()
{
	latch_input_value_ = 0;
	latch_input_counter_ = 0;
	sm_sync0_counter_ = 0;
	sync0_wd_counter_ = 0;
	sync1_wd_counter_ = 0;
	dc_running_ = false;
	sm_running_ = false;
	sm_sequence_valid_ = true;
}

/**
 * @brief 1 ms 定时上下文：推进超时与看门狗
 * @param esc ESC 硬件
 * @return 看门狗检测结果
 */
EcatSync::WatchdogResult EcatSync::Tick(EcatEsc& esc)
{
	(void)esc;

	if (!dc_sync_active_) {
		return WatchdogResult::Ok;
	}

	if (sync0_wd_value_ > 0) {
		if (sync0_wd_counter_ >= sync0_wd_value_) {
			dc_running_ = false;
		} else if (sync0_wd_counter_ < sync0_wd_value_) {
			++sync0_wd_counter_;
		}
	}
	dc_running_ = (sync0_wd_value_ == 0) || (sync0_wd_counter_ < sync0_wd_value_);

	if (dc_running_ && sync1_wd_value_ > 0) {
		if (sync1_wd_counter_ < sync1_wd_value_) {
			++sync1_wd_counter_;
		} else {
			dc_running_ = false;
		}
	}

	if (dc_running_) {
		// 同步错误上限来自 0x10F1:02，0 表示不限制
		sm_sequence_valid_ = (sync_error_limit_ == 0) || (out_->sm_event_missed < sync_error_limit_);
	} else {
		sm_sequence_valid_ = false;
		return WatchdogResult::SyncTimeout;
	}

	if (!sm_sequence_valid_) {
		return WatchdogResult::SyncTimeout;
	}
	return WatchdogResult::Ok;
}

/**
 * @brief 更新系统时间戳
 * @param esc ESC 硬件
 */
void EcatSync::UpdateTimestamp(EcatEsc& esc)
{
	if (dc_64bit_) {
		// DC 单位是 64 位，整读 8 字节
		uint8_t bytes[8] = {};

		esc.Read(reg::kSystemTime, bytes, sizeof(bytes));
		timestamp_ = wire::ReadLe64(bytes, 0);
	} else {
		// DC 单位是 32 位，只更新低 32 位并处理回绕
		const uint32_t low = ReadRegister32(esc, reg::kSystemTime);

		timestamp_ = (timestamp_ & 0xFFFFFFFF00000000ULL) | low;

		if (low < last_dc32_value_) {
			timestamp_ += 0x0000000100000000ULL;
		}
		last_dc32_value_ = low;
	}
	dc_overrun_counter_ = 0;
}

/**
 * @brief PDI 收到 SM2 输出事件
 */
void EcatSync::OnProcessOutput()
{
	sm_running_ = true;

	if (dc_sync_active_ && dc_running_) {
		sm_sync0_counter_ = 0;
	}

	if (out_->sm_event_missed > 0) {
		--out_->sm_event_missed;
	}
	in_->sm_event_missed = out_->sm_event_missed;
}

/**
 * @brief SYNC0 事件
 * @param input_pending 输出：该不该在这个 SYNC0 上提交输入
 * @return true=该调用 PublishInputs
 */
bool EcatSync::OnSync0(bool* input_pending)
{
	sync0_wd_counter_ = 0;
	dc_running_ = true;

	if (input_pending != nullptr) {
		*input_pending = false;
	}

	if (!dc_sync_active_) {
		return false;
	}

	bool pending = false;

	// 输入锁存：在一个 SYNC0 周期里数到指定次数才提交
	if (latch_input_value_ > 0) {
		if (latch_input_counter_ < latch_input_value_) {
			++latch_input_counter_;
		}
		if (latch_input_counter_ == latch_input_value_) {
			pending = true;
		}
	}

	// SM/Sync0 序列计数：一个 SM 周期内 SYNC0 过多时事件丢失计数 +3
	// SSC：上限判据用 0x10F1:02 的 sync_error_limit_
	if (sm_sync0_value_ > 0) {
		if (sm_sync0_counter_ > sm_sync0_value_) {
			if (out_size_ > 0 && out_->sm_event_missed <= sync_error_limit_) {
				out_->sm_event_missed = static_cast<uint16_t>(out_->sm_event_missed + 3U);
			}
			if (in_size_ > 0 && out_size_ == 0 && in_->sm_event_missed <= sync_error_limit_) {
				in_->sm_event_missed = static_cast<uint16_t>(in_->sm_event_missed + 3U);
			}
		}
		if (sm_sync0_counter_ <= sm_sync0_value_) {
			++sm_sync0_counter_;
		}
	}

	if (pending && latch_input_value_ == 1) {
		latch_input_counter_ = 0;
	}

	if (input_pending != nullptr) {
		*input_pending = pending;
	}
	return pending;
}

/**
 * @brief SYNC1 事件
 */
void EcatSync::OnSync1()
{
	sync1_wd_counter_ = 0;
	latch_input_counter_ = 0;
}

/**
 * @brief 把同步错误状态写回 0x1C32:32 / 0x1C33:32
 */
void EcatSync::UpdateErrorStatus()
{
	if (!bound_) {
		return;
	}

	// SSC COE_UpdateSyncErrorStatus：上限来自 0x10F1:02（sync_error_limit_）
	out_->sync_error = (out_->cycle_exceeded > 0 || out_->sm_event_missed > sync_error_limit_) ? 1U : 0U;
	in_->sync_error = (in_->cycle_exceeded > 0 || in_->sm_event_missed > sync_error_limit_) ? 1U : 0U;
}

/**
 * @brief 从 DC 控制寄存器推导同步类型
 * @param control 0x0980 的值
 * @return 主站实际配置的同步方式
 */
SyncType EcatSync::DeriveSyncType(uint32_t control)
{
	const bool unit_active = (control & (dc_control::kSyncUnitActive | dc_control::kSyncUnitAutoActive)) != 0;

	if (!unit_active) {
		return SyncType::SmSynchron;
	}

	if ((control & dc_control::kSync1Active) != 0) {
		return SyncType::DcSync1;
	}
	return SyncType::DcSync0;
}

/**
 * @brief 校验主站配置的同步类型是否被支持
 * @param configured 0x1C3x:01 的值
 * @param supported  0x1C3x:04 的值
 * @return 0=支持，否则为 AL 状态码
 */
uint16_t EcatSync::ValidateSyncType(SyncType configured, uint16_t supported)
{
	switch (configured)
	{
		case SyncType::FreeRun:
			return ((supported & kSyncTypeFreeRunSupp) != 0) ? al_status::kNoError : al_status::kFreeRunNotSupported;

		case SyncType::SmSynchron:
			return ((supported & kSyncTypeSynchronSupp) != 0) ? al_status::kNoError : al_status::kSyncModeNotSupported;

		case SyncType::DcSync0:
			return ((supported & kSyncTypeDcSync0Supp) != 0) ? al_status::kNoError : al_status::kSyncModeNotSupported;

		case SyncType::DcSync1:
			return ((supported & kSyncTypeDcSync1Supp) != 0) ? al_status::kNoError : al_status::kSyncModeNotSupported;

		case SyncType::Sm2Synchron:
			return ((supported & kSyncTypeSynchronSupp) != 0) ? al_status::kNoError : al_status::kSyncModeNotSupported;

		default:
			return al_status::kSyncTypesNotCompatible;
	}
}

}
