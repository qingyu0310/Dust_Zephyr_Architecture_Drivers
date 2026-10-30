/**
 * @file ethercat.hpp
 * @author qingyu
 * @brief EtherCAT 从站业务入口
 * @version 1.2
 * @date 2026-10-31
 *
 * # 例程
 *
 * ```cpp
 * static ecat::EcatEscHpm    esc_ {};
 * static ecat::PhyJl1111     phy0_ {};
 * static ecat::EcatPhy* const phys[] = { &phy0_, nullptr };
 * static ecat::EthercatSlave slave_ {};
 *
 * bool thread_init()
 * {
 *     if (!slave_.Init(esc_, phys)) {
 *         return false;
 *     }
 *
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
 * @copyright Copyright (c) 2026
 *
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
	EthercatSlave()  = default;
	~EthercatSlave() = default;

	EthercatSlave(const EthercatSlave&) = delete;
	EthercatSlave& operator=(const EthercatSlave&) = delete;

	bool Init(EcatEsc& esc, EcatPhy* const* phys);		// 初始化协议栈与硬件，ESC/PHY 由调用方构造后注入
	bool Poll();										// 推一拍，立刻返回

	/**
	 * @brief 停用协议栈
	 */
	void Disable() { device_.Disable(); }

	bool      IsReady() const { return device_.IsReady(); }						// 初始化是否完成
	PdoLayout Layout()  const { return device_.Layout();  }						// 当前过程数据布局
	AlState   State()   const { return device_.State();   }						// 当前 AL 状态

	/**
	 * @brief 设置主站数据接收者
	 * @param handler 回调（PDI ISR 上下文）
	 * @param context 上下文
	 */
	void SetOutputSink(EcatDevice::OutputSink handler, void* context = nullptr) { device_.SetOutputSink(handler, context); }

	/**
	 * @brief 设置待发数据提供者
	 * @param provider 回调（PDI ISR 上下文）
	 * @param context  上下文
	 */
	void SetInputSource(EcatDevice::InputSource provider, void* context = nullptr) { device_.SetInputSource(provider, context); }

private:
	EcatDevice device_;										// 协议栈组合根
	EcatEsc*   esc_ = nullptr;								// ESC 硬件
	EcatPhy*   phys_[2]   = {nullptr, nullptr};		// 各端口 PHY
	uint8_t    phy_count_ = 0;								// 实际挂接的 PHY 数
	int64_t    next_link_poll_ = 0;							// 下次链路刷新时刻（ms）
};

}
