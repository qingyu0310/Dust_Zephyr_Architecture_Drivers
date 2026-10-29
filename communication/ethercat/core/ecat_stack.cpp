/**
 * @file ecat_stack.cpp
 * @author qingyu
 * @brief EtherCAT 从站协议栈实现
 * @version 0.1
 * @date 2026-10-29
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

	/** 主循环让出 CPU 的间隔（us） */
	constexpr uint32_t kIdleSleepUs = 100;
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
 * @brief 主循环（阻塞）
 */
void EcatStack::Run()
{
	if (!initialised_) {
		DUST_LOG_ERR("ecat: Run before Init");
		return;
	}

	int64_t next_link_poll = 0;

	while (bRunApplication == TRUE) {
		MainLoop();

		const int64_t now = k_uptime_get();
		if (now >= next_link_poll) {
			hal_.UpdateLink();
			next_link_poll = now + kLinkPollMs;
		}

		/* 让出 CPU：High 优先级的纯忙循环会饿死启动器和 shell 线程 */
		k_usleep(kIdleSleepUs);
	}
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
