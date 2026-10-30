/**
 * @file ecat_stack.hpp
 * @author qingyu
 * @brief EtherCAT 从站协议栈生命周期 — 对应 usb_dev_port
 * @version 0.1
 * @date 2026-10-29
 *
 * 职责：
 *   - SSC 初始化顺序（照可跑的参考实现，不许改）
 *   - 主循环驱动（MainLoop + 周期链路刷新）
 *   - 中断分发（PDI/SYNC0/SYNC1 → SSC 的 *_Isr）
 *   - 三个业务回调的持有与转发
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "interface/ecat_hal.hpp"
#include "interface/ecat_types.hpp"

/**
 * @brief EtherCAT 从站协议栈
 */
class EcatStack final
{
public:
	using CycleHandler = std::function<void()>;                       // 每周期业务回调
	using RxHandler    = std::function<void(const uint8_t*, size_t)>; // 主站→从站（SM2）
	using TxProvider   = std::function<void(uint8_t*, size_t)>;       // 从站→主站（SM3）

	static EcatStack& Instance();

	EcatStack(const EcatStack&) = delete;
	EcatStack& operator=(const EcatStack&) = delete;

	/**
	 * @brief 初始化协议栈
	 * @return true=成功
	 */
	bool Init();

	/**
	 * @brief 推一拍协议栈：跑一次 MainLoop + 到期就刷新链路
	 *
	 * 单拍，立刻返回。循环节拍由 Thread 外壳的周期参数控制。
	 * 业务 Task() 每拍调一次，不要写成 while 死循环。
	 *
	 * @return true=协议栈仍在运行；false=已停止
	 */
	bool Poll();

	/**
	 * @brief 请求停止
	 */
	void Stop();

	void SetCycleHandler(CycleHandler handler);
	void SetRxHandler(RxHandler handler);
	void SetTxProvider(TxProvider handler);

	PdoLayout GetPdoLayout() const { return layout_; }

	/* 以下仅供 APPL_* 钩子调用，业务请用上面的 Set* 系列 */
	void OnOutputMapping(uint16_t* pData);
	void OnInputMapping(uint16_t* pData);
	void OnApplication();

private:
	EcatStack() = default;
	~EcatStack() = default;

	static void OnIrq(void* ctx, EcatIrq irq);
	void        DispatchIrq(EcatIrq irq);

	EcatHal&     hal_ = GetDefaultEcatHal();
	PdoLayout    layout_ {};
	CycleHandler cycle_handler_ {};
	RxHandler    rx_handler_ {};
	TxProvider   tx_handler_ {};
	int64_t      next_link_poll_ = 0;       // 下次刷新链路的时刻（ms）
	bool         initialised_ = false;
};
