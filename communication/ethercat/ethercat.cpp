/**
 * @file ethercat.cpp
 * @author qingyu
 * @brief EtherCAT 从站业务入口实现
 * @version 1.3
 * @date 2026-10-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ethercat.hpp"

#include <zephyr/kernel.h>

#include "ecat_phy.hpp"
#include "interface/ecat_esc.hpp"
#include "spec/ecat_limits.hpp"

namespace ecat {

/**
 * @brief 初始化协议栈与硬件
 * @param esc  ESC 硬件
 * @param phys 两个端口的 PHY，按端口顺序
 * @return true=成功
 */
bool EthercatSlave::Init(EcatEsc& esc, EcatPhy* const* phys)
{
	// ESC 由协议栈内部 Open（带事件回调），之后 MDIO 通道才可用
	if (!device_.Init(esc)) {
		return false;
	}

	esc_ = &esc;

	phy_count_ = 0;

	PhyConfig cfg {};

	cfg.advertise = 0x0100;
	cfg.autoneg   = true;

	for (uint8_t port = 0; port < kPortCount; ++port)
	{
		phys_[port] = phys[port];

		if (phys_[port] == nullptr) {
			continue;
		}

		phy_count_++;
		cfg.mdio_address = port;

		if (!phys_[port]->Init(esc, cfg)) {
			return false;
		}
	}

	next_link_poll_ = 0;
	return true;
}

/**
 * @brief 推一拍（单拍，立刻返回）
 * @return true=仍在运行
 */
bool EthercatSlave::Poll()
{
	const int64_t now = k_uptime_get();

	if (phy_count_ > 0 && now >= next_link_poll_)
	{
		for (uint8_t port = 0; port < phy_count_; ++port)
		{
			bool up = false;

			if (phys_[port]->LinkUp(*esc_, up)) {
				esc_->SetLinkState(port, up);
			} else {
				esc_->SetLinkState(port, false);
			}
		}
		next_link_poll_ = now + kLinkPollMs;
	}

	return device_.Poll();
}

}
