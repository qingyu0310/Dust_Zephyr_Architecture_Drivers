/**
 * @file ethercat.hpp
 * @author qingyu
 * @brief EtherCAT 从站业务入口
 * @version 1.1
 * @date 2026-10-31
 *
 * # 分层
 *
 * ```
 * 业务线程 / trd_xxx.cpp
 *   ↓ 只认识下面这 5 个方法
 * EthercatSlave（本文件）    门面：挂回调、单拍轮询
 *   ↓ 值成员
 * EcatDevice（core）         组合根：九个协议域 + 事件分发
 *   ↓ 纯虚契约
 * EcatEsc                    ESC 寄存器、SII 介质、MDIO
 *   ↑ 实现
 * drivers/ethercat/hal/      HPM 硬件实现
 * ```
 *
 * # 用法
 *
 * ```cpp
 * static ecat::EthercatSlave slave_;
 *
 * bool thread_init()
 * {
 *     if (!slave_.Init(esc_)) {
 *         return false;
 *     }
 *     slave_.SetOutputSink(OnMasterData, nullptr);
 *     slave_.SetInputSource(FillInputs, nullptr);
 *     return true;
 * }
 *
 * static TaskResult Task(void* context)
 * {
 *     slave_.Poll();      // 单拍，立刻返回
 *     return TaskResult::Continue;
 * }
 * ```
 *
 * # 回调上下文
 *
 * | 回调 | 触发时机 | 执行上下文 | 能做什么 |
 * |---|---|---|---|
 * | OutputSink | SM2 收到主站数据 | **PDI ISR** | 只做拷贝/赋值 |
 * | InputSource | SM3 提交前 | **PDI ISR** | 只做拷贝/赋值 |
 *
 * 两个回调都在中断里跑。这是刻意的：1 kHz 控制环要求帧到即算，
 * 挪到线程会把抖动从亚微秒退化到数百微秒。所以回调**不许阻塞、
 * 不许分配、不许打日志**。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "ecat_device.hpp"

class EcatPhy;

namespace ecat {
	
/**
 * @brief EtherCAT 从站
 */
class EthercatSlave final
{
public:
	EthercatSlave() = default;
	~EthercatSlave() { Disable(); }

	EthercatSlave(const EthercatSlave&) = delete;
	EthercatSlave& operator=(const EthercatSlave&) = delete;

	/**
	 * @brief 初始化协议栈与硬件
	 *
	 * ESC 与 PHY 由调用方构造后注入，本对象只持有抽象指针。
	 *
	 * @param esc  ESC 硬件
	 * @param phys 两个端口的 PHY，按端口顺序；没有 PHY 的端口传 nullptr
	 * @return true=成功
	 */
	bool Init(EcatEsc& esc, EcatPhy* const* phys);

	/**
	 * @brief 推一拍（单拍，立刻返回）
	 *
	 * 循环节拍由 Thread 外壳控制。任务体不许写 while 死循环——
	 * 那样线程框架的统计和故障策略全部失效。
	 *
	 * @return true=仍在运行
	 */
	bool Poll();

	/**
	 * @brief 停用协议栈
	 *
	 * 只做生命周期清理，不宣称具备 Stop→Init 热重启能力。
	 */
	void Disable();

	/**
	 * @brief 初始化是否完成
	 */
	bool IsReady() 	   const { return device_.IsReady(); }

	/**
	 * @brief 当前过程数据布局
	 */
	PdoLayout Layout() const { return device_.Layout(); }

	/**
	 * @brief 当前 AL 状态
	 *
	 * 注意与 `IsReady()` 区分：`IsReady` 表示初始化完成，
	 * 状态表示总线上现在处于哪个阶段。
	 */
	AlState State()    const { return device_.State(); }

	/**
	 * @brief 主站数据接收者（PDI ISR 上下文）
	 */
	void SetOutputSink(EcatDevice::OutputSink handler, void* context = nullptr);

	/**
	 * @brief 待发数据提供者（PDI ISR 上下文）
	 */
	void SetInputSource(EcatDevice::InputSource provider, void* context = nullptr);

private:
	EcatDevice device_;
	EcatEsc*   esc_     = nullptr;						// ESC 硬件
	EcatPhy*   phys_[2] = {nullptr, nullptr};	// 各端口 PHY
	uint8_t    phy_count_      = 0;						// 实际挂接的 PHY 数
	int64_t    next_link_poll_ = 0;						// 下次链路刷新时刻（ms）
};

}
