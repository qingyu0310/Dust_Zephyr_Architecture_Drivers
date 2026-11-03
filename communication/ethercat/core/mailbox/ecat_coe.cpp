/**
 * @file ecat_coe.cpp
 * @author qingyu
 * @brief CoE 服务选择实现
 * @version 0.2
 * @date 2026-11-03
 *
 * @copyright Copyright (c) 2026
 */

#include "mailbox/ecat_coe.hpp"

#include "spec/ecat_wire.hpp"

namespace ecat {

/**
 * @brief 解析 CoE 服务类型，不调用协议服务
 * @param frame 收到的邮箱帧
 * @return 服务类型或邮箱错误码
 */
EcatCoe::Selection EcatCoe::Select(const MailboxFrame* frame)
{
	if (frame == nullptr || frame->data == nullptr) {
		return { Service::None, mailbox::kErrorInvalidSize };
	}
	if (frame->length < kMailboxHeaderSize + coe::kHeaderSize) {
		return { Service::None, mailbox::kErrorSizeTooShort };
	}
	const uint16_t header = wire::ReadLe16(frame->data, kMailboxHeaderSize);
	const uint8_t service = static_cast<uint8_t>((header & coe::kServiceMask) >> coe::kServiceShift);

	switch (service)
	{
		case coe::kServiceSdoRequest:
			return { Service::Sdo,  0 };

		case coe::kServiceSdoInfo:
			return { Service::Info, 0 };

		case coe::kServiceEmergency:
		case coe::kServiceSdoResponse:
		case coe::kServiceTxPdo:
		case coe::kServiceRxPdo:
		case coe::kServiceTxPdoRemReq:
		case coe::kServiceRxPdoRemReq:
			return { Service::None, mailbox::kErrorServiceNotSupported };

		default:
			return { Service::None, mailbox::kErrorInvalidHeader };
	}
}

}
