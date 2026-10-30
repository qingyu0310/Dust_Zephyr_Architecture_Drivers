/**
 * @file ethercat.cpp
 * @author qingyu
 * @brief EtherCAT 从站业务入口实现
 * @version 1.2
 * @date 2026-10-31
 *
 * 门面不实现协议，也不碰任何具体板卡：ESC 与 PHY 都由调用方构造后注入，
 * 这里只持有抽象指针，链路轮询在这里按 kLinkPollMs 推进。
 *
 * @copyright Copyright (c) 2026
 */

#include "ethercat.hpp"

#include <zephyr/kernel.h>

#include "ecat_phy.hpp"
#include "interface/ecat_esc.hpp"
#include "spec/ecat_limits.hpp"

namespace ecat {

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

	for (uint8_t port = 0; port < 2; ++port)
	{
		phys_[port] = phys[port];

		if (phys_[port] == nullptr) {
			continue;
		}

		++phy_count_;
		cfg.mdio_address = port;

		if (!phys_[port]->Init(esc, cfg)) {
			return false;
		}
	}

	next_link_poll_ = 0;
	return true;
}

bool EthercatSlave::Poll()
{
	const int64_t now = k_uptime_get();

	if (phy_count_ > 0 && now >= next_link_poll_)
	{
		for (uint8_t port = 0; port < phy_count_; ++port)
		{
			bool up = false;

			if (phys_[port]->LinkUp(*esc_, up)) 
			{
				esc_->SetLinkState(port, up);
			} 
			else 
			{
				esc_->SetLinkState(port, false);
			}
		}
		next_link_poll_ = now + kLinkPollMs;
	}

	return device_.Poll();
}

void EthercatSlave::Disable()
{
	device_.Disable();
}

void EthercatSlave::SetOutputSink(EcatDevice::OutputSink handler, void* context)
{
	device_.SetOutputSink(handler, context);
}

void EthercatSlave::SetInputSource(EcatDevice::InputSource provider, void* context)
{
	device_.SetInputSource(provider, context);
}

}
