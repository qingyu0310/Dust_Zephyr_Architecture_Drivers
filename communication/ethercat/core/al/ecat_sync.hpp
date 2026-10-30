/**
 * @file ecat_sync.hpp
 * @author qingyu
 * @brief 同步与时基：SM 参数、分布式时钟、看门狗、周期测量
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "spec/ecat_application_objects.hpp"
#include "ecat_types.hpp"
#include "interface/ecat_esc.hpp"

namespace ecat {

/**
 * @brief 同步域
 *
 * 持有 0x1C32/0x1C33 的数据引用、SM/Sync0 序列计数、看门狗计数与周期测量状态；
 * 寄存器访问由调用方传入 EcatEsc&；退态只返回结果，由状态机执行。
 */
class EcatSync final
{
public:
	enum class WatchdogResult : uint8_t
	{
		Ok,				// 正常
		SmTimeout,		// SM 事件超时，应退回 SAFEOP
		SyncTimeout,	// SYNC0/1 事件超时，应退回 SAFEOP
	};

	EcatSync() = default;

	EcatSync(const EcatSync&) = delete;
	EcatSync& operator=(const EcatSync&) = delete;

	void BindParameters(ObjectData::SyncParameters& out, ObjectData::SyncParameters& in);	// 绑定 0x1C32/0x1C33 的数据槽
	void ResetParameters();																	// 装载 0x1C32/0x1C33 的初值

	uint16_t PrepareSm(EcatEsc& esc, uint16_t output_size, uint16_t input_size);														// 转换到 PREOP 前准备同步参数
	uint16_t StartSm(EcatEsc& esc);															// 进入 SAFEOP 时启动同步监控
	void     StopSm();																		// 退出 SAFEOP 时停止同步监控
	WatchdogResult Tick(EcatEsc& esc);														// 1 ms 定时上下文：推进超时与看门狗

	void UpdateTimestamp(EcatEsc& esc);														// 更新系统时间戳
	void OnProcessOutput();																	// PDI 收到 SM2 输出事件
	bool OnSync0(bool* input_pending);														// SYNC0 事件，返回该不该提交输入
	void OnSync1();																			// SYNC1 事件
	void UpdateErrorStatus();																// 把同步错误状态写回 0x1C32:32 / 0x1C33:32

	void SetSyncErrorLimit(uint16_t limit) { sync_error_limit_ = limit; }					// 设置同步错误计数上限
	void ResetUserSyncType() { sync_set_by_user_ = false; }									// 清掉主站写过的同步类型
	bool IsDcActive() const { return dc_sync_active_; }										// 当前是否 DC 同步生效
	uint64_t Timestamp() const { return timestamp_; }										// 0x10F8 当前值
	bool IsSyncSequenceValid() const { return sm_sequence_valid_; }							// SM/Sync0 序列是否有效
	bool HasProcessWatchdog() const { return process_wd_value_ > 0; }						// 主站是否配置了过程数据看门狗
	bool IsInputLatchedOnSync1() const { return dc_sync_active_ && in_ != nullptr && in_->sync_type == static_cast<uint16_t>(SyncType::DcSync1); }	// 输入是否由 SYNC1 锁存
	bool IsInputSmSynchron() const															// 输入是否走 SM/SM2 同步（DC 模式返回 false）
	{
		if (in_ == nullptr) {
			return true;
		}

		const uint16_t type = in_->sync_type;

		return type == static_cast<uint16_t>(SyncType::SmSynchron) || type == static_cast<uint16_t>(SyncType::Sm2Synchron) || type == static_cast<uint16_t>(SyncType::FreeRun);
	}

	const ObjectData::SyncParameters& OutputParameters() const { return *out_; }			// SM2 参数（0x1C32）
	const ObjectData::SyncParameters& InputParameters()  const { return *in_; }				// SM3 参数（0x1C33）

private:
	static SyncType DeriveSyncType(uint32_t control);										// 从 DC 控制寄存器推导同步类型
	static uint16_t ValidateSyncType(SyncType configured, uint16_t supported);				// 校验同步类型是否被支持
	
	ObjectData::SyncParameters* out_ = nullptr;			// 字典里 0x1C32 的数据
	ObjectData::SyncParameters* in_  = nullptr;			// 字典里 0x1C33 的数据
	bool bound_ = false;								// 是否已绑定到字典

	uint16_t sm_sync0_counter_ = 0;						// SYNC0 计数，SM 事件时清零
	uint16_t sm_sync0_value_ = 0;						// 一个 SM 周期内允许的 SYNC0 数，0=不检查
	bool     sm_sequence_valid_ = true;					// 序列是否有效
	uint16_t latch_input_value_ = 0;					// 在第几个 SYNC0 上提交输入，0=由 SYNC1 触发
	uint16_t latch_input_counter_ = 0;					// 当前计数

	uint16_t sync0_wd_counter_ = 0;						// SYNC0 看门狗计数
	uint16_t sync0_wd_value_ = 0;						// SYNC0 看门狗阈值
	uint16_t sync1_wd_counter_ = 0;						// SYNC1 看门狗计数
	uint16_t sync1_wd_value_ = 0;						// SYNC1 看门狗阈值

	bool dc_sync_active_ = false;						// DC 同步生效
	bool dc_running_ = false;							// 已收到 SYNC0 事件
	bool sm_running_ = false;							// 已收到 SM2 事件
	bool sync_set_by_user_ = false;						// 0x1C3x:01 是否被主站写过

	uint32_t cycle_time_start_ = 0;						// 本轮测量起点（ns）
	uint32_t min_cycle_time_start_ = 0;					// 最小周期测量起点
	uint32_t min_cycle_time_value_ = 0;					// 最小周期累计值
	uint32_t system_time_read_failure_ = 0;				// 读系统的抖动，用于补偿
	bool     min_cycle_measurement_started_ = false;	// 最小周期测量已开始

	uint64_t timestamp_ = 0;							// 0x10F8 的值
	uint32_t last_dc32_value_ = 0;						// 上一次读到的 32 位 DC 时间
	bool     dc_64bit_ = true;							// DC 单位是否 64 位（0x0008.3 = 0）
	uint32_t dc_overrun_counter_ = 0;					// 距离上次刷时间戳的毫秒数

	uint16_t sync_error_limit_ = 0;						// 同步错误上限，0=不限制
	uint16_t process_wd_value_ = 0;						// 过程数据看门狗超时（ms），0=未启用
	uint16_t out_size_ = 0;								// 输出过程数据字节数（PrepareSm 传入）
	uint16_t in_size_  = 0;								// 输入过程数据字节数（PrepareSm 传入）
};

}
