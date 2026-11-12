/**
 * @file ethercat.hpp
 * @author qingyu
 * @brief EtherCAT 从站业务入口（模板门面）
 * @version 2.1
 * @date 2026-11-07
 *
 * 收发 PDO 的业务类型由模板参数固定，核心只认字节与字数。
 * 模板成员函数必须对被实例化单元可见，因此实现内联在本文件。
 *
 * # 例程
 *
 * ```cpp
 * struct PdoOutputs { uint32_t counter[64]; };
 * struct PdoInputs  { uint32_t counter[64]; };
 *
 * static ecat::EcatEscHpm esc {};
 * static ::PhyRtl8201f phy0 {};
 * static ecat::EthercatSlave<PdoOutputs, PdoInputs> slave {};
 *
 * bool thread_init()
 * {
 *     return slave.Init(esc, { { 0, phy0 } });
 * }
 *
 * static TaskResult Task(void* context)
 * {
 *     PdoInputs inputs {};
 *
 *     inputs.counter[0] = counter++;
 *     slave.SetInputs(inputs);
 *     slave.Poll();      // 单拍，立刻返回
 *
 *     const PdoOutputs& outputs = slave.Outputs();
 *     return TaskResult::Continue;
 * }
 * ```
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>

#include "ecat_device.hpp"
#include "ecat_phy.hpp"
#include "ecat_esc.hpp"
#include "ecat_profile.hpp"
#include "spec/ecat_limits.hpp"

namespace ecat {

/**
 * @brief 真实端口与 PHY 实体，实体由 trd 持有
 */
struct PhyPort
{
	uint8_t  port = 0;			// 真实 ESC 端口号
	EcatPhy* device = nullptr;	// PHY 实体，寿命覆盖从站
};

/**
 * @brief EtherCAT 从站
 * @tparam Rx 主站→从站业务结构体
 * @tparam Tx 从站→主站业务结构体
 */
template <typename Rx, typename Tx>
class EthercatSlave final
{
public:
	using Profile = SlaveProfile<Rx, Tx>;

	EthercatSlave() = default;
	~EthercatSlave() = default;

	EthercatSlave(const EthercatSlave&) = delete;
	EthercatSlave& operator=(const EthercatSlave&) = delete;

	/**
	 * @brief 初始化协议栈与硬件
	 * @param esc  ESC 实体，寿命覆盖从站运行期
	 * @param phys 真实端口与 PHY 实体，按端口下标排列，空口传 nullptr
	 * @return true=成功
	 */
	bool Init(EcatEsc* esc, EcatPhy* const* phys)
	{
		if (device_.IsReady() || esc == nullptr) {
			return true;
		}

		DeviceProfile profile {};

		profile.rx_bytes = Profile::kRxBytes;
		profile.tx_bytes = Profile::kTxBytes;
		profile.rx_words = Profile::kRxWords;
		profile.tx_words = Profile::kTxWords;

		// 先准备协议栈并打开 ESC（不带事件），MDIO 通道此时才可用
		if (!device_.Prepare(esc, profile)) {
			return false;
		}

		esc_ = esc;
		phy_count_ = 0;

		PhyConfig cfg {};

		cfg.advertise = 0x0100;
		cfg.autoneg   = true;

		// PHY 在事件开启前初始化：失败时逆序清理，不留下可运行的 Device
		for (uint8_t port = 0; port < kPortCount; ++port)
		{
			phys_[port] = phys[port];

			if (phys_[port] == nullptr) {
				continue;
			}

			++phy_count_;
			cfg.mdio_address = esc->GetPhyAddress(port);

			if (!phys_[port]->Init(*esc, cfg)) {
				device_.Disable();
				return false;
			}
		}

		// 业务回调与 PHY 全部就绪后才发布 ready 并开中断
		device_.SetOutputSink (OnOutput,  this);
		device_.SetInputSource(FillInput, this);

		if (!device_.Start()) {
			device_.Disable();
			return false;
		}

		next_link_poll_ = 0;
		return true;
	}

	/**
	 * @brief 推一拍（单拍，立刻返回）
	 * @return true=仍在运行
	 */
	bool Poll()
	{
		if (!device_.IsReady()) {
			return false;
		}

		const int64_t now = k_uptime_get();

		if (now >= next_link_poll_)
		{
			for (uint8_t port = 0; port < kPortCount; ++port)
			{
				EcatPhy* phy = phys_[port];

				if (phy == nullptr) {
					continue;
				}

				bool up = false;

				if (phy->LinkUp(*esc_, up)) {
					esc_->SetLinkState(port, up);
				} else {
					esc_->SetLinkState(port, false);
				}
			}
			next_link_poll_ = now + kLinkPollMs;
		}

		return device_.Poll();
	}

	/**
	 * @brief 停用协议栈
	 */
	void Disable() { device_.Disable(); }

	bool    IsReady() const { return device_.IsReady(); }					// 初始化是否完成
	AlState State()   const { return device_.State();   }					// 当前 AL 状态

	uint32_t Sm2Count() const { return device_.Sm2Count(); }				// 主站写入 SM2 累计次数
	uint32_t Sm3Count() const { return device_.Sm3Count(); }				// 主站读走 SM3 累计次数

	/**
	 * @brief 最近一帧主站数据
	 * @return 业务镜像（PDI ISR 写入）
	 */
	const Rx& Outputs() const { return outputs_; }

	/**
	 * @brief 写入待发数据
	 * @param inputs 业务数据（线程侧），下一拍由 PDI ISR 拷出
	 */
	void SetInputs(const Tx& inputs) { inputs_ = inputs; }

	bool RequestFaultReport() { return device_.RequestFaultReport(); }		// 输出保存的首次故障
	void ClearFaultReport()   { device_.ClearFaultReport(); 		 }		// 清除保存现场，允许保存下一次故障

private:
	/**
	 * @brief 主站数据到达（PDI ISR 上下文），拷进业务镜像
	 * @param context 从站实例
	 * @param data    主站发来的字节
	 * @param length  字节数
	 */
	static void OnOutput(void* context, const uint8_t* data, uint16_t length)
	{
		if (length != Profile::kRxBytes) {
			return;
		}

		std::memcpy(&static_cast<EthercatSlave*>(context)->outputs_, data, Profile::kRxBytes);
	}

	/**
	 * @brief 待发数据取走（PDI ISR 上下文），从业务镜像拷出
	 * @param context 从站实例
	 * @param data    目标缓冲
	 * @param length  字节数
	 */
	static void FillInput(void* context, uint8_t* data, uint16_t length)
	{
		if (length != Profile::kTxBytes) {
			return;
		}

		std::memcpy(data, &static_cast<EthercatSlave*>(context)->inputs_, Profile::kTxBytes);
	}

	EcatEsc*   esc_ = nullptr;					// ESC 硬件，Init 时绑定
	EcatPhy*   phys_[kPortCount] = {};			// 各端口 PHY，按真实端口下标，空口为 nullptr
	uint8_t    phy_count_ = 0;					// 实际挂接的 PHY 数
	EcatDevice device_  {};						// 协议栈组合根
	Rx         outputs_ {};						// 主站→从站业务镜像
	Tx         inputs_  {};						// 从站→主站业务镜像
	int64_t    next_link_poll_ = 0;				// 下次链路刷新时刻（ms）
};

}
