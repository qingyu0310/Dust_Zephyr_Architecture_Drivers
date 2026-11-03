/**
 * @file ecat_coe.hpp
 * @author qingyu
 * @brief CoE 服务选择：解析报头，不持有服务对象
 * @version 0.2
 * @date 2026-11-03
 *
 * 来源：SSC ecatcoe.c 的服务分类；服务调用与应答提交由父层完成。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "mailbox/ecat_mailbox.hpp"

namespace ecat {

/**
 * @brief 无状态的 CoE 请求分类
 */
class EcatCoe final
{
public:
	enum class Service : uint8_t
	{
		None,	// 无可处理服务
		Sdo,	// SDO 请求
		Info,	// SDO Information 请求
	};

	struct Selection
	{
		Service service = Service::None;	// 请求对应的服务
		uint8_t code = 0;					// 非零为邮箱错误码
	};

	static Selection Select(const MailboxFrame* frame);	// 校验报头并返回服务类型
};

}
