/**
 * @file ecat_coe.hpp
 * @author qingyu
 * @brief CoE 服务路由：按 CoE 头把报文分给 SDO 或 SDO Info
 * @version 0.1
 * @date 2026-10-30
 *
 * 来源：`ssc/ecatcoe.c` 的 COE_Init / COE_ServiceInd / COE_ContinueInd。
 * 纯路由，不拥有任何状态——C 版的 `pCoeSendStored` 是邮箱级背压，
 * 本实现的邮箱域自带队列，不需要 CoE 再存一份。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "mailbox/ecat_mailbox.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"

namespace ecat {

/**
 * @brief CoE 服务路由
 */
class EcatCoe final
{
public:
	EcatCoe() = default;

	EcatCoe(const EcatCoe&) = delete;
	EcatCoe& operator=(const EcatCoe&) = delete;

	void Bind(EcatSdo& sdo, EcatSdoInfo& sdo_info);					// 绑定 SDO 与 SDO Info

	static uint8_t Dispatch(void* context, MailboxFrame* frame);	// 邮箱分发入口，context 为本对象

	uint8_t Continue(MailboxFrame* frame);							// 推进未发完的 SDO Info 分片

private:
	uint8_t Service(void* context, MailboxFrame* frame);

	EcatSdo*     sdo_ = nullptr;		// SDO 服务
	EcatSdoInfo* sdo_info_ = nullptr;	// SDO Info 服务
};

}
