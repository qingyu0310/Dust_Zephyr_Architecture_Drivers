/**
 * @file ethercat.hpp
 * @author qingyu
 * @brief EtherCAT 从站顶层封装 — 继承 Stream，串联 EcatStack + EcatHal
 * @version 0.3
 * @date 2026-10-29
 *
 * # 分层（与 usb.hpp 同构）
 *
 * ```
 * trd_test.cpp              业务：注册回调
 *   ↓
 * EthercatSlave (本文件)     Stream 接口 + 三个业务回调转发
 *   ↓
 * EcatStack (core)          SSC 生命周期（MainInit / MainLoop / ISR 分发）
 *   ↓
 * EcatHal (interface)       纯虚基类，零厂商头
 *   ↑ 继承
 * EcatHalHpm                子树 drivers 里的厂商实现（ESC + PHY）
 * ```
 *
 * # 用法
 *
 * ```cpp
 * static ecat::EthercatSlave slave;
 *
 * bool thread_init()
 * {
 *     if (!slave.Init()) {
 *         return false;
 *     }
 *     slave.SetRxHandler([](const uint8_t *data, size_t len) {
 *         // 主站本周期发来的字节
 *     });
 *     slave.SetTxProvider([](uint8_t *data, size_t len) {
 *         // 把状态写进 data[0..len-1]
 *     });
 *     return true;
 * }
 *
 * static TaskResult Task(void *ctx)
 * {
 *     slave.Run();                // 阻塞
 *     return TaskResult::Continue;
 * }
 * ```
 *
 * # 回调语义
 *
 * | 回调 | 触发时机 | 由谁调用 | 必须做什么 |
 * |---|---|---|---|
 * | RxHandler | 主站数据到达（SM2 更新后） | SSC APPL_OutputMapping | 解析本周期输入 |
 * | TxProvider | 协议栈要发数据前（SM3 提交前） | SSC APPL_InputMapping | 填满输出缓冲 |
 * | CycleHandler | 每周期应用阶段 | SSC APPL_Application | 可选，业务节拍 |
 *
 * 三个回调都在 SSC 的协议上下文中调用，必须短、不能阻塞、不能 sleep。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "core/ecat_stack.hpp"
#include "interface/ecat_types.hpp"

namespace ecat
{

/**
 * @brief EtherCAT 从站驱动
 */
class EthercatSlave final
{
public:
	EthercatSlave() = default;
	~EthercatSlave() = default;

	EthercatSlave(const EthercatSlave&) = delete;
	EthercatSlave& operator=(const EthercatSlave&) = delete;

	/**
	 * @brief 初始化协议栈与硬件
	 * @return true=成功
	 */
	bool Init();

	/**
	 * @brief 运行主循环（阻塞，直到 Stop）
	 */
	void Run();

	/**
	 * @brief 请求停止
	 */
	void Stop();

	/**
	 * @brief 当前 PDO 布局（Init 之后有效）
	 */
	PdoLayout GetPdoLayout() const { return stack_.GetPdoLayout(); }

	void SetCycleHandler(EcatStack::CycleHandler handler)
	{
		stack_.SetCycleHandler(std::move(handler));
	}

	void SetRxHandler(EcatStack::RxHandler handler)
	{
		stack_.SetRxHandler(std::move(handler));
	}

	void SetTxProvider(EcatStack::TxProvider handler)
	{
		stack_.SetTxProvider(std::move(handler));
	}

private:
	EcatStack& stack_ = EcatStack::Instance();
};

} // namespace ecat
