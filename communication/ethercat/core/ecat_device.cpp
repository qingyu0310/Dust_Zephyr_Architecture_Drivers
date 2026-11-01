/**
 * @file ecat_device.cpp
 * @author qingyu
 * @brief 从站协议组合根实现
 * @version 0.2
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 */

#include "ecat_device.hpp"

#include <zephyr/kernel.h>

#include "mailbox/ecat_coe.hpp"
#include "model/ecat_dictionary.hpp"
#include "spec/ecat_limits.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "pdo/ecat_process_data.hpp"
#include "spec/ecat_registers.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"
#include "mailbox/ecat_sii_service.hpp"
#include "al/ecat_sm.hpp"
#include "al/ecat_state_machine.hpp"
#include "al/ecat_sync.hpp"
#include "spec/ecat_wire.hpp"
#include "interface/ecat_esc.hpp"
#include "log.hpp"

namespace ecat {

EcatDevice::EcatDevice() = default;

EcatDevice::~EcatDevice() = default;

/**
 * @brief 抓取软件计数和 ESC 实际状态，不读取会确认 AL Control 的寄存器
 * @return 当前诊断快照；硬件寄存器为顺序读取，不是硬件同时锁存
 */
EcatDevice::DiagnosticSnapshot EcatDevice::CaptureDiagnosticSnapshot()
{
	DiagnosticSnapshot sample {};
	const unsigned key = irq_lock();

	sample.time_ms = k_uptime_get_32();
	sample.pdi = static_cast<uint32_t>(atomic_get(&diagnostic_pdi_count_));
	sample.sm2 = static_cast<uint32_t>(atomic_get(&diagnostic_sm2_count_));
	sample.sm3 = static_cast<uint32_t>(atomic_get(&diagnostic_sm3_count_));
	sample.ticks = static_cast<uint32_t>(atomic_get(&diagnostic_tick_count_));
	sample.age_ms = (sample.sm2 != 0) ? sample.time_ms - static_cast<uint32_t>(atomic_get(&diagnostic_last_sm2_ms_)) : 0;
	sample.poll_gap_ms = diagnostic_max_poll_gap_ms_;
	sample.state = static_cast<uint16_t>(state_machine_.Current());
	sample.code = state_machine_.StatusCode();
	sample.output = state_machine_.IsOutputRunning();
	sample.events = esc_->ReadAlEvent();

	irq_unlock(key);

	const uint16_t addresses[] = { reg::kAlStatus, reg::kAlStatusCode, reg::kDlStatus, reg::kAlEventMask };
	uint32_t values[4] = {};

	for (uint8_t i = 0; i < 4; ++i)
	{
		uint8_t bytes[4] = {};

		esc_->Read(addresses[i], bytes, sizeof(bytes));
		values[i] = wire::ReadLe32(bytes, 0);
	}
	sample.hw_state = static_cast<uint16_t>(values[0]);
	sample.hw_code = static_cast<uint16_t>(values[1]);
	sample.dl_status = static_cast<uint16_t>(values[2]);
	sample.event_mask = values[3];
	return sample;
}

/**
 * @brief 首次退出 OP 时冻结历史并保存硬件现场，输出由线程分批完成
 */
void EcatDevice::CaptureDiagnosticFault()
{
	// 保护首次现场，只有用户明确清除后才允许覆盖。
	if (atomic_get(&diagnostic_fault_available_) != 0) {
		return;
	}
	diagnostic_fault_ = CaptureDiagnosticSnapshot();
	diagnostic_fault_history_size_ = diagnostic_history_size_;
	const uint8_t first = static_cast<uint8_t>((diagnostic_history_next_ + kDiagnosticHistoryCount - diagnostic_history_size_) % kDiagnosticHistoryCount);

	for (uint8_t i = 0; i < diagnostic_fault_history_size_; ++i) {
		diagnostic_fault_history_[i] = diagnostic_history_[(first + i) % kDiagnosticHistoryCount];
	}

	const uint16_t addresses[] = { reg::kWdDivider, reg::kPdWdTime, reg::kPdWdState };

	for (uint8_t i = 0; i < 3; ++i)
	{
		uint8_t bytes[4] = {};

		esc_->Read(addresses[i], bytes, sizeof(bytes));
		diagnostic_fault_registers_[i] = wire::ReadLe32(bytes, 0);
	}

	const SyncManagerRegisters sm2 = ReadSyncManager(*esc_, sync_manager::kProcessOut);
	const SyncManagerRegisters sm3 = ReadSyncManager(*esc_, sync_manager::kProcessIn);

	diagnostic_fault_registers_[3] = sm2.address_length;
	diagnostic_fault_registers_[4] = sm2.settings;
	diagnostic_fault_registers_[5] = sm3.address_length;
	diagnostic_fault_registers_[6] = sm3.settings;
	diagnostic_fault_registers_[7] = static_cast<uint32_t>(atomic_get(&diagnostic_max_sm2_gap_));
	++diagnostic_fault_id_;
	diagnostic_dump_line_ = 0;
	diagnostic_additional_faults_ = 0;
	diagnostic_next_dump_ms_ = k_uptime_get_32();
	atomic_set(&diagnostic_fault_available_, 1);
	DUST_LOG_ERR("ecat fault saved: id=%u t=%u code=0x%04x; run 'ecat fault'", static_cast<unsigned>(diagnostic_fault_id_), static_cast<unsigned>(diagnostic_fault_.time_ms), static_cast<unsigned>(diagnostic_fault_.code));
}

/**
 * @brief 请求线程重放保存的首次故障，不在 shell 线程直接访问诊断缓冲
 * @return true=请求已提交；false=当前没有保存的故障
 */
bool EcatDevice::RequestFaultReport()
{
	if (atomic_get(&diagnostic_fault_available_) == 0) {
		return false;
	}
	atomic_set(&diagnostic_report_request_, 1);
	return true;
}

/**
 * @brief 每 50 ms 输出一行冻结现场，避免一次输出多行挤满八条日志记录池
 */
void EcatDevice::ReportDiagnosticFault()
{
	if (!diagnostic_dump_pending_) {
		return;
	}

	const uint32_t now = k_uptime_get_32();

	if (static_cast<int32_t>(now - diagnostic_next_dump_ms_) < 0) {
		return;
	}
	diagnostic_next_dump_ms_ = now + 50U;

	const DiagnosticSnapshot& fault = diagnostic_fault_;
	const uint16_t line = diagnostic_dump_line_++;

	switch (line)
	{
		case 0:
			DUST_LOG_ERR("ecat fault id=%u t=%u age_ms=%u poll_gap_ms=%u", static_cast<unsigned>(diagnostic_fault_id_), static_cast<unsigned>(fault.time_ms), static_cast<unsigned>(fault.age_ms), static_cast<unsigned>(fault.poll_gap_ms));
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
			const uint32_t divider = diagnostic_fault_registers_[0] & 0xFFFFU;
			const uint32_t time = diagnostic_fault_registers_[1] & 0xFFFFU;
			const uint32_t timeout_ms = static_cast<uint32_t>(((static_cast<uint64_t>(divider) + 2U) * time + 24999U) / 25000U);

			DUST_LOG_ERR("ecat fault wd: divider=%u time=%u timeout_ms=%u raw=0x%08x", static_cast<unsigned>(divider), static_cast<unsigned>(time), static_cast<unsigned>(timeout_ms), static_cast<unsigned>(diagnostic_fault_registers_[2]));
			return;
		}
		case 5:
			DUST_LOG_ERR("ecat fault SM2: addr=0x%04x len=%u settings=0x%08x", static_cast<unsigned>(diagnostic_fault_registers_[3] & 0xFFFFU), static_cast<unsigned>(diagnostic_fault_registers_[3] >> 16), static_cast<unsigned>(diagnostic_fault_registers_[4]));
			return;
		case 6:
			DUST_LOG_ERR("ecat fault SM3: addr=0x%04x len=%u settings=0x%08x", static_cast<unsigned>(diagnostic_fault_registers_[5] & 0xFFFFU), static_cast<unsigned>(diagnostic_fault_registers_[5] >> 16), static_cast<unsigned>(diagnostic_fault_registers_[6]));
			return;
		case 7:
			DUST_LOG_ERR("ecat fault max_sm2_gap_ms=%u", static_cast<unsigned>(diagnostic_fault_registers_[7]));
			return;
		case 8:
			DUST_LOG_ERR("ecat history begin: id=%u samples=%u period_ms=250", static_cast<unsigned>(diagnostic_fault_id_), static_cast<unsigned>(diagnostic_fault_history_size_));
			return;
		default:
			break;
	}

	const uint16_t history_line = static_cast<uint16_t>(line - 9U);
	const uint8_t sample_index = static_cast<uint8_t>(history_line / 5U);

	if (sample_index >= diagnostic_fault_history_size_)
	{
		DUST_LOG_ERR("ecat history end: id=%u lines=%u additional_faults=%u", static_cast<unsigned>(diagnostic_fault_id_), static_cast<unsigned>(line + 1U), static_cast<unsigned>(diagnostic_additional_faults_));
		diagnostic_dump_pending_ = false;
		return;
	}

	const DiagnosticSnapshot& sample = diagnostic_fault_history_[sample_index];

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

/**
 * @brief 初始化协议栈与硬件
 *
 * 顺序不能反：先把域之间的引用接好，再打开硬件。一旦 Open 成功中断随时进来，
 * 而中断路径会调 OnPdi → 状态机 → 各域，此时域还没绑定就会走进空指针。
 *
 * @param esc ESC 硬件
 * @param sii SII 介质
 * @return true=成功
 */
bool EcatDevice::Init(EcatEsc& esc)
{
	if (ready_) {
		return true;
	}

	esc_ = &esc;

	sync_.BindParameters(dictionary_.Data().sync_manager_out, dictionary_.Data().sync_manager_in);
	sync_.ResetParameters();

	state_machine_.Bind(mailbox_, process_data_, sync_, sii_service_);
	state_machine_.BindDictionary(dictionary_);

	dictionary_.BindStateQuery(QueryState, this);
	dictionary_.BindTimestamp(QueryTimestamp, this);

	sdo_.Bind(dictionary_, SubmitReply, this);
	sdo_info_.Bind(dictionary_, SubmitReply, this);

	coe_.Bind(sdo_, sdo_info_);
	mailbox_.SetDispatcher(DispatchMailbox, this);
	sii_service_.Bind(esc);

	EcatEsc::Config config {};

	if (!esc.Open(config, OnEscEvent, this)) {
		return false;
	}

	// SSC ECAT_Init：先禁用全部 SM 通道，再清 AL 事件掩码，最后置 INIT
	{
		uint8_t comm[4] = {};

		esc.Read(reg::kCommInfo, comm, sizeof(comm));

		const uint32_t comm_info = wire::ReadLe32(comm, 0);
		const uint8_t max_sm = static_cast<uint8_t>((comm_info & comm_info::kSmChannelsMask) >> comm_info::kSmChannelsShift);

		for (uint8_t i = 0; i < max_sm; ++i) {
			DisableSyncManagerChannel(esc, i);
		}

		// SSC nMaxSyncMan：状态机读剩余通道确认 SM_ACT 时要用实际通道数
		if (max_sm > 0) {
			state_machine_.SetSyncManagerCount(max_sm);
		}
	}

	state_machine_.Reset(esc);
	sii_service_.Init();

	esc.StartEvents();

	ready_ = true;
	DUST_LOG_INF("ecat: slave ready, %u sync managers", static_cast<unsigned>(state_machine_.SyncManagerCount()));
	return true;
}

/**
 * @brief 停用协议栈并关闭硬件事件
 */
void EcatDevice::Disable()
{
	if (!ready_) {
		return;
	}

	ready_ = false;

	esc_->StopEvents();
	sii_service_.Flush();
	mailbox_.Stop(*esc_);
	sync_.StopSm();

	// SSC：停用要清掉整个协议会话，不能只停事件和邮箱
	state_machine_.Reset(*esc_);
	sdo_.Abort();
	sdo_info_.Reset();
	process_data_.Clear();
}

/**
 * @brief 线程上下文：推一拍
 *
 * 顺序与 MainLoop + ECAT_Main 对应：邮箱收发、AL 事件、错误检查、链路轮询。
 *
 * @return true=仍在运行
 */
bool EcatDevice::Poll()
{
	if (!ready_) {
		return false;
	}

	const atomic_val_t report_request = atomic_set(&diagnostic_report_request_, 0);

	if (report_request == 2)
	{
		diagnostic_dump_pending_ = false;
		atomic_clear(&diagnostic_fault_available_);
	}
	else if (report_request == 1 && atomic_get(&diagnostic_fault_available_) != 0)
	{
		diagnostic_dump_line_ = 0;
		diagnostic_next_dump_ms_ = k_uptime_get_32();
		diagnostic_dump_pending_ = true;
	}

	const uint32_t poll_ms = k_uptime_get_32();

	if (diagnostic_poll_seen_)
	{
		const uint32_t gap = poll_ms - diagnostic_last_poll_ms_;

		if (gap > diagnostic_max_poll_gap_ms_) {
			diagnostic_max_poll_gap_ms_ = gap;
		}
	}
	diagnostic_last_poll_ms_ = poll_ms;
	diagnostic_poll_seen_ = true;

	mailbox_.Service(*esc_);
	HandleAlEvents(esc_->ReadAlEvent());
	state_machine_.CheckTimeout(*esc_);
	state_machine_.CheckErrors(*esc_);

	const bool output_running = state_machine_.IsOutputRunning();

	if (diagnostic_output_running_ && !output_running)
	{
		if (atomic_get(&diagnostic_fault_available_) == 0) {
			CaptureDiagnosticFault();
		} else {
			++diagnostic_additional_faults_;
		}
	}
	diagnostic_output_running_ = output_running;

	const uint32_t sample_ms = k_uptime_get_32();

	if (!diagnostic_dump_pending_ && static_cast<int32_t>(sample_ms - diagnostic_next_sample_ms_) >= 0)
	{
		diagnostic_history_[diagnostic_history_next_] = CaptureDiagnosticSnapshot();
		diagnostic_max_poll_gap_ms_ = 0;
		diagnostic_history_next_ = static_cast<uint8_t>((diagnostic_history_next_ + 1U) % kDiagnosticHistoryCount);

		if (diagnostic_history_size_ < kDiagnosticHistoryCount) {
			++diagnostic_history_size_;
		}
		diagnostic_next_sample_ms_ = sample_ms + 250U;
	}
	ReportDiagnosticFault();

	const int64_t now = k_uptime_get();

	if (now >= diagnostic_next_report_)
	{
		diagnostic_next_report_ = now + 1000;

		// 状态快照保持一致，诊断不读 AL Control，也不改变事件处理路径。
		const unsigned key    = irq_lock();
		const unsigned state  = static_cast<unsigned>(state_machine_.Current());
		const unsigned code   = state_machine_.StatusCode();
		const unsigned output = state_machine_.IsOutputRunning();
		const unsigned events = esc_->ReadAlEvent();
		const bool has_error  = state_machine_.HasError();
		
		const uint32_t pdi = static_cast<uint32_t>(atomic_get(&diagnostic_pdi_count_));
		const uint32_t sm2 = static_cast<uint32_t>(atomic_get(&diagnostic_sm2_count_));
		const uint32_t sm3 = static_cast<uint32_t>(atomic_get(&diagnostic_sm3_count_));
		const uint32_t age = (sm2 != 0) ? k_uptime_get_32() - static_cast<uint32_t>(atomic_get(&diagnostic_last_sm2_ms_)) : 0;
		const uint32_t max_gap = static_cast<uint32_t>(atomic_get(&diagnostic_max_sm2_gap_));

		irq_unlock(key);

		const bool receiving = output != 0 && sm2 != diagnostic_previous_sm2_;

		if (receiving) {
			diagnostic_received_ = true;
		}

		if (has_error || code != 0 || (diagnostic_received_ && !receiving))
		{
			// 每次停收只报告一次，之后静默；完整现场由 ecat fault 主动读取。
			if (!diagnostic_rx_failed_)
			{
				CaptureDiagnosticFault();
				DUST_LOG_ERR("ecat rx stopped: state=0x%02x code=0x%04x output=%u event=0x%04x", state, code, output, events);
				DUST_LOG_ERR("ecat rx stalled: age_ms=%u pdi=%u sm2=%u sm3=%u", static_cast<unsigned>(age), static_cast<unsigned>(pdi), static_cast<unsigned>(sm2), static_cast<unsigned>(sm3));
			}
			diagnostic_rx_failed_ = true;
		}
		else if (receiving)
		{
			if (diagnostic_rx_failed_) {
				DUST_LOG_INF("ecat rx resumed: state=0x%02x sm2=%u", state, static_cast<unsigned>(sm2));
			}
			diagnostic_rx_failed_ = false;
			DUST_LOG_INF("ecat diag: state=0x%02x code=0x%04x output=%u event=0x%04x", state, code, output, events);
			DUST_LOG_INF("ecat diag: pdi=%u sm2=%u", static_cast<unsigned>(pdi), static_cast<unsigned>(sm2));
			DUST_LOG_INF("ecat diag: last_sm2_age_ms=%u max_gap_ms=%u sm3=%u", static_cast<unsigned>(age), static_cast<unsigned>(max_gap), static_cast<unsigned>(sm3));
		}

		// 尚未首次建链时保持安静，不把正常 INIT 等待误报成故障。
		diagnostic_previous_sm2_ = sm2;
	}

	return true;
}

/**
 * @brief ISR：PDI 中断，读 AL 事件并分发
 */
void EcatDevice::OnPdi()
{
	atomic_inc(&diagnostic_pdi_count_);

	if (!ready_) {
		return;
	}

	HandleAlEvents(esc_->ReadAlEvent());
}

/**
 * @brief ISR：SYNC0，按锁存点提交输入
 */
void EcatDevice::OnSync0()
{
	if (!ready_) {
		return;
	}

	bool input_pending = false;

	sync_.OnSync0(&input_pending);

	if (input_pending && state_machine_.IsInputRunning()) 
	{
		// 从 SM 寄存器现读地址，主站可能在 PREOP 下改过 SM 配置
		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		process_data_.PublishInputs(*esc_, registers.Address());
	}
}

/**
 * @brief ISR：SYNC1，清看门狗与锁存计数
 */
void EcatDevice::OnSync1()
{
	if (!ready_) {
		return;
	}

	sync_.OnSync1();

	// SSC Sync1_Isr：DC SYNC1 且锁存点由 SYNC1 触发时，在这里提交输入
	if (state_machine_.IsInputRunning() && process_data_.Layout().tx_size > 0 && sync_.IsInputLatchedOnSync1())
	{
		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		process_data_.PublishInputs(*esc_, registers.Address());
	}
}

/**
 * @brief 定时器：1 ms，推进状态机超时与 SII 落盘
 */
void EcatDevice::OnTick1ms()
{
	atomic_inc(&diagnostic_tick_count_);

	if (!ready_) {
		return;
	}

	// SSC ECAT_CheckTimer：ESM 超时递减
	state_machine_.Tick();

	// SSC ECAT_CheckTimer：DC 看门狗 + 时间戳累加
	sync_.Tick(*esc_);
	sync_.UpdateTimestamp(*esc_);

	// SSC ECAT_CheckTimer：EEPROM 延迟落盘
	sii_service_.Tick();
}

/**
 * @brief ESC 复位事件：停事件、回 INIT
 *
 * 复位之后 ESC 的 AL 状态寄存器已经是 INIT，软件不同步就是撒谎。
 */
void EcatDevice::OnReset()
{
	if (!ready_) {
		return;
	}

	esc_->StopEvents();

	// 复位后要清掉整个会话，否则残留的 SDO 分段/Info 分片会带进新会话
	mailbox_.Stop(*esc_);
	sync_.StopSm();
	state_machine_.Reset(*esc_);
	sdo_.Abort();
	sdo_info_.Reset();
	process_data_.Clear();

	// 清完再重开事件：只停不开会留下 ready_=true 但 IRQ 永久关闭
	esc_->StartEvents();
}

/**
 * @brief 当前过程数据布局
 * @return 布局快照
 */
PdoLayout EcatDevice::Layout() const
{
	return process_data_.Layout();
}

/**
 * @brief 当前 AL 状态
 * @return 状态
 */
AlState EcatDevice::State() const
{
	return state_machine_.Current();
}

/**
 * @brief 业务输出接收者
 * @param handler 回调
 * @param context 上下文
 */
void EcatDevice::SetOutputSink(OutputSink handler, void* context)
{
	process_data_.SetOutputSink(handler, context);
}

/**
 * @brief 业务输入提供者
 * @param handler 回调
 * @param context 上下文
 */
void EcatDevice::SetInputSource(InputSource handler, void* context)
{
	process_data_.SetInputSource(handler, context);
}

/**
 * @brief 硬件事件分发入口
 * @param context 本对象
 * @param event   事件源
 */
void EcatDevice::OnEscEvent(void* context, EcatEvent event)
{
	if (context == nullptr) {
		return;
	}

	auto& self = *static_cast<EcatDevice*>(context);

	switch (event)
	{
		case EcatEvent::Pdi:
			self.OnPdi();
			break;
		case EcatEvent::Sync0:
			self.OnSync0();
			break;
		case EcatEvent::Sync1:
			self.OnSync1();
			break;
		case EcatEvent::Tick1ms:
			self.OnTick1ms();
			break;
		case EcatEvent::Reset:
		default:
			self.OnReset();
			break;
	}
}

/**
 * @brief 字典的状态查询回调
 * @param context 本对象
 * @return 当前 AL 状态
 */
AlState EcatDevice::QueryState(void* context)
{
	if (context == nullptr) {
		return AlState::Init;
	}
	return static_cast<EcatDevice*>(context)->State();
}

/**
 * @brief 字典的 0x10F8 时间戳回调
 * @param context 本对象
 * @return 当前时间戳
 */
uint64_t EcatDevice::QueryTimestamp(void* context)
{
	if (context == nullptr) {
		return 0;
	}
	return static_cast<EcatDevice*>(context)->sync_.Timestamp();
}

/**
 * @brief 邮箱的协议分发回调
 * @param context 本对象
 * @param frame   收到的报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatDevice::DispatchMailbox(void* context, struct MailboxFrame* frame)
{
	if (context == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	auto& self = *static_cast<EcatDevice*>(context);

	return EcatCoe::Dispatch(&self.coe_, frame);
}

/**
 * @brief SDO 应答提交
 *
 * 应答就写在收到的那个帧的缓冲里（原地改写），缓冲本来就在池里，
 * 所以这里只需把长度改好并交给邮箱队列，由 Service 在下一拍推进。
 *
 * @param context 本对象
 * @param data    应答报文
 * @param length  应答字节数
 * @return 0=已入队
 */
uint8_t EcatDevice::SubmitReply(void* context, uint8_t* data, uint16_t length)
{
	if (context == nullptr || data == nullptr) {
		return mailbox::kErrorInvalidSize;
	}

	auto& self = *static_cast<EcatDevice*>(context);

	MailboxFrame frame {};

	frame.data = data;
	frame.length = length;
	frame.capacity = kMaxMailboxSize;

	const EcatMailbox::SubmitResult result = self.mailbox_.Submit(&frame);

	// 应答提交失败是真实故障，要能看到
	if (result != EcatMailbox::SubmitResult::Queued) {
		DUST_LOG_ERR("ecat: reply submit failed len=%u", static_cast<unsigned>(length));
	}


	return (result == EcatMailbox::SubmitResult::Queued) ? 0 : mailbox::kErrorNoMoreMemory;
}

/**
 * @brief 处理 AL 事件寄存器的各个位
 * @param events 0x0220 的值
 */
void EcatDevice::HandleAlEvents(uint16_t events)
{
	// AL Control：读 0x0120 即确认，必须整 32 位读
	if ((events & al_event::kAlControl) != 0) {
		uint8_t control[4] = {};

		esc_->Read(reg::kAlControl, control, sizeof(control));
		state_machine_.HandleControl(*esc_, control[0]);

		// SSC：处理 AL Control 时同时消费 SM_CHANGE（转换里已经检查过 SM 设置），
		// 否则同一份快照会被重复处理一次
		events = static_cast<uint16_t>(events & ~al_event::kSmChange);
	}

	// SM Change：SM 配置被主站改写，按当前状态重新检查配置
	// SSC 门控：无 pending 转换、无 AL 错误、当前不是 INIT 才单独处理
	if ((events & al_event::kSmChange) != 0 && state_machine_.CanRecheck()) {
		state_machine_.Recheck(*esc_);
	}

	// EEPROM 命令
	if ((events & al_event::kEepromCommand) != 0) {
		sii_service_.PollCommand();
	}

	// 过程数据部分与 ISR 共用同一份实现
	HandlePdiEvents(events);

	// 邮箱写事件：主站已把报文写进 SM0，立刻取回来交给协议层
	if ((events & al_event::kMailboxWrite) != 0) {
		mailbox_.Receive(*esc_);
	}

	// 邮箱读事件：主站读走了 SM1，推进下一帧
	if ((events & al_event::kMailboxRead) != 0) {
		mailbox_.OnSendMailboxRead(*esc_);
	}
}

/**
 * @brief 处理过程数据相关事件（ISR 与线程共用）
 *
 * SSC PDI_Isr 只做这些：输出映射、输入提交、事件承认。
 *
 * @param events 0x0220 的值
 */
void EcatDevice::HandlePdiEvents(uint16_t events)
{
	// 过程数据输出：无论哪个状态，收到 SM2 事件都算已收到首帧
	if ((events & al_event::kProcessOutput) != 0)
	{
		const unsigned key = irq_lock();
		const uint32_t now_ms = k_uptime_get_32();

		if (atomic_get(&diagnostic_sm2_count_) != 0)
		{
			const uint32_t gap = now_ms - static_cast<uint32_t>(atomic_get(&diagnostic_last_sm2_ms_));

			if (gap > static_cast<uint32_t>(atomic_get(&diagnostic_max_sm2_gap_))) {
				atomic_set(&diagnostic_max_sm2_gap_, static_cast<atomic_val_t>(gap));
			}
		}
		atomic_set(&diagnostic_last_sm2_ms_, static_cast<atomic_val_t>(now_ms));
		atomic_inc(&diagnostic_sm2_count_);
		irq_unlock(key);

		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessOut);

		state_machine_.MarkFirstOutputs(*esc_);

		if (state_machine_.IsOutputRunning())
		{
			process_data_.ConsumeOutputs(*esc_, registers.Address());
			sync_.OnProcessOutput();
		} else {
			// 非 OP 下也要承认事件，否则 ESC 不再产生下次中断
			AcknowledgeSyncManagerArea(*esc_, registers.Address(), process_data_.Layout().rx_size);
		}

		// SSC PDI_Isr：只有输入同步类型是 SM/SM2 同步时才在这里提交输入；
		// DC 模式由 SYNC0/SYNC1 锁存点提交，不能两处都发
		if (state_machine_.IsInputRunning() && process_data_.Layout().tx_size > 0 && sync_.IsInputSmSynchron())
		{
			const SyncManagerRegisters in_registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

			process_data_.PublishInputs(*esc_, in_registers.Address());
		}
	}

	// 过程数据输入：主站读走 SM3 后让缓冲重新可用
	if ((events & al_event::kProcessInput) != 0)
	{
		atomic_inc(&diagnostic_sm3_count_);

		const SyncManagerRegisters registers = ReadSyncManager(*esc_, sync_manager::kProcessIn);

		AcknowledgeSyncManagerArea(*esc_, registers.Address(), process_data_.Layout().tx_size);
	}
}

}
