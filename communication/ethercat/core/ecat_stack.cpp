/**
 * @file ecat_stack.cpp
 * @author qingyu
 * @brief EtherCAT 从站协议栈实现
 * @version 0.3
 * @date 2026-10-30
 *
 * framework 里唯一 include SSC 生命周期头的地方（appl/ 除外）。
 *
 * @copyright Copyright (c) 2026
 */

#include "ecat_stack.hpp"

#include <zephyr/kernel.h>

#include "core/ecat_eeprom.hpp"
#include "core/ecat_pdo.hpp"
#include "log.hpp"

extern "C" {

#include "applInterface.h"
#include "ecatappl.h"
#include "ecatslv.h"

}

namespace
{
	/** 链路刷新周期（ms） */
	constexpr int64_t kLinkPollMs = 100;
}

EcatStack& EcatStack::Instance()
{
	static EcatStack instance;
	return instance;
}

/**
 * @brief 初始化协议栈
 *
 * 顺序照可跑的参考实现 main()，不许改：
 *   hal.Init → MainInit → 重装 EEPROM 钩子 → PDO 映射
 *   → UpdateLink → bRunApplication=TRUE → hal.Start
 *
 * @return true=成功
 */
bool EcatStack::Init()
{
	if (initialised_) {
		return true;
	}

	EcatHal::Config cfg {};

	if (!hal_.Init(cfg, OnIrq, this)) {
		DUST_LOG_ERR("ecat: hal init failed");
		return false;
	}

	if (MainInit() != 0U) {
		DUST_LOG_ERR("ecat: MainInit failed");
		return false;
	}

	/* MainInit 会清掉 EEPROM 钩子，必须在它之后重新装 */
	EcatEeprom::Install(hal_);

	layout_ = EcatPdo::Generate();
	if (layout_.rx_size == 0 && layout_.tx_size == 0) {
		DUST_LOG_ERR("ecat: pdo mapping failed");
		return false;
	}

	hal_.UpdateLink();
	bRunApplication = TRUE;
	hal_.Start();

	initialised_ = true;
	DUST_LOG_INF("ecat: stack ready rx=%u tx=%u", static_cast<unsigned>(layout_.rx_size), static_cast<unsigned>(layout_.tx_size));
	return true;
}

/**
 * @brief 推一拍协议栈（单拍，立刻返回）
 *
 * 循环节拍交给 Thread 外壳，任务体不许自己 while。
 *
 * @return true=仍在运行；false=已停止
 */
bool EcatStack::Poll()
{
	if (!initialised_) {
		DUST_LOG_ERR("ecat: Poll before Init");
		return false;
	}
	if (bRunApplication != TRUE) {
		return false;
	}

	MainLoop();

	const int64_t now = k_uptime_get();
	static int64_t next_diag = 0;
	if (now >= next_diag) {
		next_diag = now + 500;

		// 同一临界区保存状态机变量，恢复中断后再打印。
		const unsigned int key = irq_lock();
		const unsigned state = nAlStatus;
		const unsigned trans = nEcatStateTrans;
		const unsigned wait = bEcatWaitForAlControlRes;
		const unsigned pending = bApplEsmPending;
		const int timeout = EsmTimeoutCounter;
		const unsigned first = bEcatFirstOutputsReceived;
		const unsigned esc_int = bEscIntEnabled;
		const unsigned dc = bDcSyncActive;
		const unsigned output = bEcatOutputUpdateRunning;
		const unsigned wd_ms = EcatWdValue;
		irq_unlock(key);

		// 只读状态寄存器，不读取会确认状态请求的 AL Control。
		uint32_t al_status = 0;
		uint32_t al_code = 0;
		uint32_t wd_status = 0;
		hal_.Read(0x0130, &al_status, sizeof(al_status));
		hal_.Read(0x0134, &al_code, sizeof(al_code));
		hal_.Read(0x0440, &wd_status, sizeof(wd_status));
		DUST_LOG_INF("ecat esm: state=%02x trans=%02x wait=%u pending=%u",
			state, trans, wait, pending);
		DUST_LOG_INF("ecat gate: first=%u irq=%u dc=%u out=%u",
			first, esc_int, dc, output);
		DUST_LOG_INF("ecat watchdog: timeout=%d wd_ms=%u wd=%08x",
			timeout, wd_ms,
			static_cast<unsigned>(wd_status));
		DUST_LOG_INF("ecat status: al=%04x code=%04x",
			static_cast<unsigned>(al_status & 0xFFFFU),
			static_cast<unsigned>(al_code & 0xFFFFU));
	}
	if (now >= next_link_poll_) {
		hal_.UpdateLink();
		next_link_poll_ = now + kLinkPollMs;
	}
	return true;
}

/**
 * @brief 请求停止
 */
void EcatStack::Stop()
{
	bRunApplication = FALSE;
	hal_.Stop();
}

void EcatStack::SetCycleHandler(CycleHandler handler)
{
	cycle_handler_ = std::move(handler);
}

void EcatStack::SetRxHandler(RxHandler handler)
{
	rx_handler_ = std::move(handler);
}

void EcatStack::SetTxProvider(TxProvider handler)
{
	tx_handler_ = std::move(handler);
}

/**
 * @brief HAL 中断回调入口
 * @param ctx  EcatStack 实例
 * @param irq  中断源
 */
void EcatStack::OnIrq(void* ctx, EcatIrq irq)
{
	if (ctx != nullptr) {
		static_cast<EcatStack*>(ctx)->DispatchIrq(irq);
	}
}

/**
 * @brief 中断分发到 SSC
 * @param irq  中断源
 */
void EcatStack::DispatchIrq(EcatIrq irq)
{
	switch (irq) {
	case EcatIrq::Pdi:
		PDI_Isr();
		break;
	case EcatIrq::Sync0:
		Sync0_Isr();
		break;
	case EcatIrq::Sync1:
		Sync1_Isr();
		break;
	case EcatIrq::Reset:
	default:
		/* SSC 无对应入口，ESC 复位由 HAL 自行处理 */
		break;
	}
}

/**
 * @brief SM2 已由协议栈读入，转交业务解析
 * @param pData SM2 过程数据首地址
 */
void EcatStack::OnOutputMapping(uint16_t* pData)
{
	if (rx_handler_) {
		rx_handler_(reinterpret_cast<const uint8_t*>(pData), layout_.rx_size);
	}
}

/**
 * @brief 业务把本周期输出填进 SM3 缓冲
 * @param pData SM3 过程数据首地址
 */
void EcatStack::OnInputMapping(uint16_t* pData)
{
	if (tx_handler_) {
		tx_handler_(reinterpret_cast<uint8_t*>(pData), layout_.tx_size);
	}
}

/**
 * @brief 每周期应用阶段
 */
void EcatStack::OnApplication()
{
	if (cycle_handler_) {
		cycle_handler_();
	}
}
