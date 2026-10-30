/**
 * @file ecat_coe.cpp
 * @author qingyu
 * @brief CoE 服务路由实现
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#include "mailbox/ecat_coe.hpp"

#include "mailbox/ecat_mailbox.hpp"
#include "mailbox/ecat_sdo.hpp"
#include "mailbox/ecat_sdo_info.hpp"
#include "spec/ecat_wire.hpp"
#include "log.hpp"

namespace ecat {

namespace {
constexpr uint16_t kCoeOffset = kMailboxHeaderSize;	// CoE 头相对报文起点的偏移
}

/**
 * @brief 绑定两个 SDO 服务
 * @param sdo      SDO 服务
 * @param sdo_info SDO Information 服务
 */
void EcatCoe::Bind(EcatSdo& sdo, EcatSdoInfo& sdo_info)
{
	sdo_ = &sdo;
	sdo_info_ = &sdo_info;
}

/**
 * @brief 处理一帧 CoE 报文
 *
 * 直接作为 `EcatMailbox::Dispatcher` 使用。
 *
 * @param context 本对象
 * @param frame   报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatCoe::Dispatch(void* context, MailboxFrame* frame)
{
	if (context == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}
	return static_cast<EcatCoe*>(context)->Service(context, frame);
}

/**
 * @brief 按 CoE 头的服务号分发给对应的 SDO 处理
 * @param context 未使用（由 Dispatch 转发，保持与回调签名一致）
 * @param frame   报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatCoe::Service(void* context, MailboxFrame* frame)
{
	(void)context;

	if (frame == nullptr || frame->data == nullptr) {
		return mailbox::kErrorInvalidSize;
	}

	// 读 CoE 头前先确认帧里真的有这 2 字节，否则会读到邮箱头之外
	if (frame->length < static_cast<uint16_t>(kCoeOffset + coe::kHeaderSize)) {
		return mailbox::kErrorSizeTooShort;
	}

	const uint16_t coe_header = wire::ReadLe16(frame->data, kCoeOffset);
	const uint8_t service = static_cast<uint8_t>((coe_header & coe::kServiceMask) >> coe::kServiceShift);


	switch (service)
	{
		case coe::kServiceSdoRequest:
			return (sdo_ != nullptr) ? sdo_->Handle(frame) : mailbox::kErrorServiceNotSupported;

		case coe::kServiceSdoInfo:
			return (sdo_info_ != nullptr) ? sdo_info_->Handle(frame) : mailbox::kErrorServiceNotSupported;

		// 以下服务本从站不支持，按 C 版返回原错误码，主站靠它判断从站能力
		case coe::kServiceEmergency:
		case coe::kServiceSdoResponse:
		case coe::kServiceTxPdo:
		case coe::kServiceRxPdo:
		case coe::kServiceTxPdoRemReq:
		case coe::kServiceRxPdoRemReq:
			return mailbox::kErrorServiceNotSupported;

		default:
			return mailbox::kErrorInvalidHeader;
	}
}

/**
 * @brief 推进尚未发完的分片
 *
 * 来源：`COE_ContinueInd`。当前只可能是 SDO Info 的后续分片。
 *
 * @param frame 可写报文
 * @return 0=有分片被发出；非 0 表示没有待发分片或失败
 */
uint8_t EcatCoe::Continue(MailboxFrame* frame)
{
	if (sdo_info_ == nullptr || !sdo_info_->HasPendingFragment()) {
		return 1;
	}
	return sdo_info_->Continue(frame);
}

}
