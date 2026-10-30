/**
 * @file ecat_sdo.cpp
 * @author qingyu
 * @brief SDO 服务实现
 * @version 0.1
 * @date 2026-10-30
 *
 * 返回值约定：0=已处理且应答已交出；kAbort* 常量=处理失败需组中止应答；
 * mailbox::kError* 只从 Handle 返回，不从中止码路径返回。
 *
 * @copyright Copyright (c) 2026
 */

#include "mailbox/ecat_sdo.hpp"

#include <cstring>

#include "model/ecat_dictionary.hpp"
#include "mailbox/ecat_mailbox.hpp"
#include "spec/ecat_limits.hpp"
#include "spec/ecat_wire.hpp"
#include "log.hpp"

namespace ecat {

namespace {

constexpr uint16_t kMailboxServiceTypeOffset   = 4;															// 邮箱头里服务类型字节的位置
constexpr uint16_t kCoeOffset 			 	   = kMailboxHeaderSize;										// CoE 头偏移（相对报文起点）
constexpr uint16_t kSdoOffset 			 	   = kMailboxHeaderSize + coe::kHeaderSize;						// SDO 头偏移（相对报文起点）
constexpr uint16_t kMinSdoLength 		 	   = kSdoOffset + 4;											// 一个 SDO 报文的最小长度
// SSC EXPEDITED_FRAME_SIZE = COE_HEADER_SIZE + INITSDO_HEADER_SIZE + MAX_EXPEDITED_DATA = 2 + 4 + 4
// 换算成含 6 字节邮箱头的总长：kSdoOffset(8) + kHeaderSize(4) + 4 = 16
constexpr uint16_t kExpeditedLength 	 	   = kSdoOffset + sdo::kHeaderSize + 4;							// expedited 报文总长（含邮箱头与 4 字节数据区）
constexpr uint16_t kInitiateNormalLength 	   = kSdoOffset + sdo::kHeaderSize + 4;							// 初始化上传/下载请求总长（同含数据区）
constexpr uint16_t kMaxInlineData 		       = kMaxMailboxSize  - kMailboxHeaderSize - coe::kHeaderSize;	// 邮箱最多能装下的 SDO 数据
constexpr uint16_t kUploadNormalResponseSize   = coe::kHeaderSize + sdo::kHeaderSize   + 4;					// SSC UPLOAD_NORM_RES_SIZE = 2 + 4 + 4 = 10
constexpr uint16_t kDownloadNormalResponseSize = kUploadNormalResponseSize;									// SSC DOWNLOAD_NORM_RES_SIZE 同值

/**
 * @brief 取邮箱头里的长度字段
 * @param frame 报文
 * @return 载荷字节数（不含头）
 */
uint16_t MailboxLength(const MailboxFrame& frame)
{
	return wire::ReadLe16(frame.data, mailbox::kLengthOffset);
}

/**
 * @brief 写邮箱头里的长度字段
 * @param frame          报文
 * @param payload_length 载荷字节数（不含头）
 */
void SetMailboxLength(MailboxFrame* frame, uint16_t payload_length)
{
	wire::WriteLe16(frame->data, mailbox::kLengthOffset, payload_length);
}

/**
 * @brief 把 CoE 头里的服务号改成"应答"
 * @param frame 报文
 */
void SetCoeResponse(MailboxFrame* frame)
{
	const uint16_t header = wire::ReadLe16(frame->data, kCoeOffset);
	const uint16_t updated = static_cast<uint16_t>((header & ~coe::kServiceMask) | (static_cast<uint16_t>(coe::kServiceSdoResponse) << coe::kServiceShift));

	wire::WriteLe16(frame->data, kCoeOffset, updated);
}

}

/**
 * @brief 绑定字典与应答通道
 * @param dictionary 对象字典
 * @param sink       应答提交回调
 * @param context    回调上下文
 */
void EcatSdo::Bind(EcatDictionary& dictionary, ReplySink sink, void* context)
{
	dictionary_ = &dictionary;
	sink_ = sink;
	sink_context_ = context;
}

/**
 * @brief 处理一帧 SDO 请求
 * @param frame 收到的报文
 * @return 0=已处理；非 0 为邮箱错误码
 */
uint8_t EcatSdo::Handle(MailboxFrame* frame)
{
	if (frame == nullptr || frame->data == nullptr || dictionary_ == nullptr) {
		return mailbox::kErrorServiceNotSupported;
	}

	const uint16_t payload_length = MailboxLength(*frame);
	const uint16_t total = static_cast<uint16_t>(payload_length + kMailboxHeaderSize);


	if (total < kMinSdoLength || total > frame->length) {
		return mailbox::kErrorInvalidSize;
	}

	// 上一个 SDO 还没结束：本实现没有异步应答，in_work_ 恒为 false，保留判断给后续接入
	if (in_work_) {
		return mailbox::kErrorServiceInWork;
	}

	const uint8_t command_byte = frame->data[kSdoOffset];
	const uint8_t command = static_cast<uint8_t>((command_byte & sdo::kServiceMask) >> sdo::kServiceShift);
	const uint16_t index = wire::ReadLe16(frame->data, kSdoOffset + 1);
	const uint8_t sub_index = frame->data[kSdoOffset + 3];

	uint32_t abort_code = 0;

	switch (command)
	{
		case sdo::kServiceDownload:
		case sdo::kServiceUpload:
			abort_code = HandleInitiate(command, frame, frame->data + kSdoOffset, total);
			break;

		case sdo::kServiceDownloadSegment:
		case sdo::kServiceUploadSegment:
			// 分段请求必须与当前会话的分段方向一致
			if (command != segment_command_) {
				abort_code = sdo::kAbortGeneralError;
			} else if (command == sdo::kServiceDownloadSegment) {
				abort_code = HandleDownloadSegment(frame, frame->data + kSdoOffset, total);
			} else {
				abort_code = HandleUploadSegment(frame, frame->data + kSdoOffset);
			}
			break;

		default:
			abort_code = sdo::kAbortGeneralError;
			break;
	}

	if (abort_code != 0) {
		DUST_LOG_ERR("ecat: sdo abort 0x%08x idx=0x%04x sub=%u", static_cast<unsigned>(abort_code), static_cast<unsigned>(index), static_cast<unsigned>(sub_index));
		BuildAbort(frame, abort_code, index, sub_index);
		Reply(frame, static_cast<uint16_t>(kSdoOffset + sdo::kHeaderSize + 4));
	}
	return 0;
}

/**
 * @brief 处理初始化下载/上传请求
 * @param command 命令字节
 * @param frame   报文
 * @param payload 报文数据首地址
 * @param length  报文长度
 * @return 0=成功，否则中止码
 */
uint32_t EcatSdo::HandleInitiate(uint8_t command, MailboxFrame* frame, uint8_t* payload, uint16_t length)
{
	const uint8_t command_byte = payload[0];
	const uint16_t index = wire::ReadLe16(payload, 1);
	const uint8_t sub_index = payload[3];
	const bool complete_access = (command_byte & sdo::kCompleteAccess) != 0;

	const ObjectSlot* slot = dictionary_->Find(index);

	if (slot == nullptr) {
		return sdo::kAbortNonexistentObject;
	}

	// Complete Access 只允许子索引 0 或 1
	if (complete_access && sub_index > 1) {
		return sdo::kAbortUnsupportedAccess;
	}

	if (slot->code != ObjectCode::Variable) {
		const uint16_t max_sub = dictionary_->SubCountOf(*slot);

		if (sub_index > max_sub) {
			return sdo::kAbortSubIndexNotExisting;
		}
	}

	const uint32_t object_length = complete_access ? dictionary_->TotalLengthOf(*slot, sub_index) : dictionary_->LengthOf(*slot, sub_index);

	if (command == sdo::kServiceUpload) {
		return BuildUpload(frame, payload, length, index, sub_index, complete_access, object_length);
	}
	return BuildDownload(frame, payload, length, index, sub_index, complete_access);
}

/**
 * @brief 构造上传应答
 * @param frame           报文
 * @param payload         SDO 头首地址
 * @param length          报文长度
 * @param index           对象索引
 * @param sub_index       子索引
 * @param complete_access 是否整对象
 * @param object_length   对象字节数
 * @return 0=已应答；否则中止码
 */
uint32_t EcatSdo::BuildUpload(MailboxFrame* frame, uint8_t* payload, uint16_t length, uint16_t index, uint8_t sub_index, bool complete_access, uint32_t object_length)
{
	(void)length;

	// Expedited：对象放得进 4 字节
	if (object_length > 0 && object_length <= 4) {
		uint32_t written = 0;

		std::memset(payload + 4, 0, 4);

		const uint32_t result = dictionary_->Read(index, sub_index, complete_access, payload + 4, 4, &written);

		if (result != 0) {
			return result;
		}

		const uint8_t size_code = static_cast<uint8_t>((4U - written) << sdo::kExpSizeShift);

		payload[0] = static_cast<uint8_t>((sdo::kServiceUpload << sdo::kServiceShift) | sdo::kSizeIndicated | sdo::kExpedited | (complete_access ? sdo::kCompleteAccess : 0) | size_code);

		SetCoeResponse(frame);
		SetMailboxLength(frame, kExpeditedLength - kMailboxHeaderSize);

		// SSC：应答提交失败要如实上报，不能当成功吞掉
		return (Reply(frame, kExpeditedLength) == 0) ? 0 : sdo::kAbortNoMoreMemory;
	}

	// Normal Upload：一个邮箱装得下
	const uint16_t data_capacity = static_cast<uint16_t>(kMaxMailboxSize - kMailboxHeaderSize - kUploadNormalResponseSize);

	if (data_capacity >= object_length) 
	{
		uint32_t written = 0;

		const uint32_t result = dictionary_->Read(index, sub_index, complete_access, payload + sdo::kCompleteSizeOffset + 4, data_capacity, &written);

		if (result != 0) {
			return result;
		}

		payload[0] = static_cast<uint8_t>((sdo::kServiceUpload << sdo::kServiceShift) | sdo::kSizeIndicated | (complete_access ? sdo::kCompleteAccess : 0));
		wire::WriteLe16(payload, 4, static_cast<uint16_t>(object_length & 0xFFFFU));
		wire::WriteLe16(payload, 6, static_cast<uint16_t>((object_length >> 16) & 0xFFFFU));

		SetCoeResponse(frame);

		const uint16_t response_length = static_cast<uint16_t>(kMailboxHeaderSize + kUploadNormalResponseSize + object_length);

		SetMailboxLength(frame, static_cast<uint16_t>(kUploadNormalResponseSize + object_length));

		return (Reply(frame, response_length) == 0) ? 0 : sdo::kAbortNoMoreMemory;
	}

	// 分段上传
	if (object_length > kSegmentCapacity) {
		return sdo::kAbortNoMoreMemory;
	}

	uint32_t written = 0;

	const uint32_t result = dictionary_->Read(index, sub_index, complete_access, segment_buffer_, kSegmentCapacity, &written);

	if (result != 0) {
		return result;
	}

	segment_total_size_ = object_length;
	segment_handled_ = 0;
	segment_index_ = index;
	segment_sub_index_ = sub_index;
	segment_complete_access_ = complete_access;
	segment_command_ = sdo::kServiceUploadSegment;
	last_toggle_ = true;

	payload[0] = static_cast<uint8_t>(sdo::kServiceUpload << sdo::kServiceShift);
	wire::WriteLe16(payload, 4, static_cast<uint16_t>(object_length & 0xFFFFU));
	wire::WriteLe16(payload, 6, static_cast<uint16_t>((object_length >> 16) & 0xFFFFU));

	SetCoeResponse(frame);
	SetMailboxLength(frame, 10);

	return (Reply(frame, kExpeditedLength) == 0) ? 0 : sdo::kAbortNoMoreMemory;
}

/**
 * @brief 构造下载应答
 * @param frame           报文
 * @param payload         SDO 头首地址
 * @param length          报文长度
 * @param index           对象索引
 * @param sub_index       子索引
 * @param complete_access 是否整对象
 * @return 0=已应答；否则中止码
 */
uint32_t EcatSdo::BuildDownload(MailboxFrame* frame, uint8_t* payload, uint16_t length, uint16_t index, uint8_t sub_index, bool complete_access)
{
	const uint8_t command_byte = payload[0];
	const bool expedited = (command_byte & sdo::kExpedited) != 0;

	if (length != kExpeditedLength && !expedited && length < kInitiateNormalLength) {
		return sdo::kAbortParamLengthTooShort;
	}

	if (expedited) {
		if (length != kExpeditedLength) {
			return sdo::kAbortParamLengthTooShort;
		}

		// expedited 时长度编码在命令字节 bit2..3，值为 4 - 实际长度
		const uint32_t data_size = 4U - ((command_byte & sdo::kExpSizeMask) >> sdo::kExpSizeShift);
		const uint32_t result = dictionary_->Write(index, sub_index, complete_access, payload + 4, data_size);

		if (result != 0) {
			return result;
		}

		payload[0] = static_cast<uint8_t>(sdo::kServiceInitiateDownloadRes << sdo::kServiceShift);

		SetCoeResponse(frame);
		SetMailboxLength(frame, 10);

		return (Reply(frame, kExpeditedLength) == 0) ? 0 : sdo::kAbortNoMoreMemory;
	}

	// 正常下载：SDO 头后 4 字节是数据总长度
	const uint32_t download_size = static_cast<uint32_t>(wire::ReadLe16(payload, 4)) | (static_cast<uint32_t>(wire::ReadLe16(payload, 6)) << 16);
	const uint16_t inline_bytes = static_cast<uint16_t>(length - kSdoOffset - 8);

	// 一致性校验：一次性下载时 mbxSize 必须等于 10 + downloadSize；
	// 分段下载时 mbxSize 必须是整个接收邮箱
	const uint16_t payload_size = static_cast<uint16_t>(length - kMailboxHeaderSize);
	const bool segmented = (kMailboxHeaderSize + kUploadNormalResponseSize + download_size) > kMaxMailboxSize;

	if (segmented) {
		if (payload_size != kMaxMailboxSize - kMailboxHeaderSize) {
			return sdo::kAbortParamLengthTooLong;
		}
	} else if (payload_size != static_cast<uint16_t>(kUploadNormalResponseSize + download_size)) {
		return sdo::kAbortParamLengthTooShort;
	}

	// 分段下载：把首段数据存进分段缓冲
	if (download_size > inline_bytes) {
		if (download_size > kSegmentCapacity) {
			return sdo::kAbortNoMoreMemory;
		}
		if (inline_bytes > kSegmentCapacity) {
			return sdo::kAbortParamLengthTooLong;
		}

		std::memcpy(segment_buffer_, payload + sdo::kCompleteSizeOffset + 4, inline_bytes);

		segment_total_size_ = download_size;
		segment_handled_ = inline_bytes;
		segment_index_ = index;
		segment_sub_index_ = sub_index;
		segment_complete_access_ = complete_access;
		segment_command_ = sdo::kServiceDownloadSegment;
		last_toggle_ = true;

		payload[0] = static_cast<uint8_t>(sdo::kServiceInitiateDownloadRes << sdo::kServiceShift);

		SetCoeResponse(frame);
		SetMailboxLength(frame, 10);

		return (Reply(frame, kExpeditedLength) == 0) ? 0 : sdo::kAbortNoMoreMemory;
	}

	// 一次性下载
	const uint32_t result = dictionary_->Write(index, sub_index, complete_access, payload + sdo::kCompleteSizeOffset + 4, inline_bytes);

	if (result != 0) {
		return result;
	}

	payload[0] = static_cast<uint8_t>(sdo::kServiceInitiateDownloadRes << sdo::kServiceShift);

	SetCoeResponse(frame);
	SetMailboxLength(frame, 10);

	return (Reply(frame, kExpeditedLength) == 0) ? 0 : sdo::kAbortNoMoreMemory;
}

/**
 * @brief 处理下载分段
 * @param frame   报文
 * @param payload 数据首地址
 * @param length  报文长度
 * @return 0=成功，否则中止码
 */
uint32_t EcatSdo::HandleDownloadSegment(MailboxFrame* frame, uint8_t* payload, uint16_t length)
{
	if (segment_command_ != sdo::kServiceDownloadSegment) {
		return sdo::kAbortGeneralError;
	}

	const uint8_t header = payload[0];
	const bool toggle = (header & sdo::kSegmentToggle) != 0;

	// toggle 必须相对上一段翻转
	if (toggle == last_toggle_) {
		return sdo::kAbortGeneralError;
	}
	last_toggle_ = toggle;

	const bool last_segment = (header & sdo::kSegmentNoMoreFollows) != 0;
	const uint16_t available = static_cast<uint16_t>(length - kSdoOffset - sdo::kSegmentHeaderSize);
	const uint32_t bytes_to_save = segment_total_size_ - segment_handled_;

	uint16_t copy_size = available;

	if (last_segment) {
		// 剩余数据 <= 7 时，有效字节数由报头编码给出（7 - 有效字节数）；
		// 否则长度必须与报文完全一致，报头编码被忽略
		const uint16_t encoded = static_cast<uint16_t>((header & sdo::kSegmentSizeMask) >> sdo::kSegmentSizeShift);

		if (bytes_to_save <= sdo::kMinSegmentedData) {
			copy_size = static_cast<uint16_t>(sdo::kMinSegmentedData - encoded);
		} else {
			copy_size = available;
		}

		if (copy_size > available) {
			copy_size = available;
		}
	} else if (bytes_to_save <= available) {
		// 还有后续段，但剩余数据装得进这一段 —— 主站的长度字段不一致
		Abort();
		return sdo::kAbortParamLengthTooLong;
	}

	if (static_cast<uint32_t>(segment_handled_) + copy_size > kSegmentCapacity) {
		Abort();
		return sdo::kAbortNoMoreMemory;
	}

	std::memcpy(segment_buffer_ + segment_handled_, payload + sdo::kSegmentHeaderSize, copy_size);
	segment_handled_ += copy_size;

	if (last_segment) {
		if (segment_handled_ != segment_total_size_) {
			Abort();
			return sdo::kAbortParamLengthTooShort;
		}

		const uint32_t result = dictionary_->Write(segment_index_, segment_sub_index_, segment_complete_access_, segment_buffer_, segment_total_size_);

		Abort();

		if (result != 0) {
			return result;
		}
	}

	// 分段下载应答：CoE 头 + 分段头，载荷长度按 SSC DOWNLOAD_NORM_RES_SIZE = 10
	payload[0] = static_cast<uint8_t>((sdo::kServiceDownloadSegmentRes << sdo::kServiceShift) | (toggle ? sdo::kSegmentToggle : 0));

	SetCoeResponse(frame);
	SetMailboxLength(frame, kDownloadNormalResponseSize);

	return (Reply(frame, static_cast<uint16_t>(kMailboxHeaderSize + kDownloadNormalResponseSize)) == 0) ? 0 : sdo::kAbortNoMoreMemory;
}

/**
 * @brief 处理上传分段
 * @param frame   报文
 * @param payload 数据首地址
 * @return 0=成功，否则中止码
 */
uint32_t EcatSdo::HandleUploadSegment(MailboxFrame* frame, uint8_t* payload)
{
	if (segment_command_ != sdo::kServiceUploadSegment) {
		return sdo::kAbortGeneralError;
	}

	const uint8_t header = payload[0];
	const bool toggle = (header & sdo::kSegmentToggle) != 0;

	if (toggle == last_toggle_) {
		return sdo::kAbortGeneralError;
	}
	last_toggle_ = toggle;

	const uint32_t remaining = segment_total_size_ - segment_handled_;
	const uint16_t chunk_capacity = static_cast<uint16_t>(kMaxMailboxSize - kMailboxHeaderSize - coe::kHeaderSize - sdo::kSegmentHeaderSize);

	uint16_t chunk = chunk_capacity;
	bool last_segment = true;

	// SSC: nSdoSegCompleteSize < (nSdoSegBytesToHandle + maxData) 时这一段就是最后一段
	if (remaining < chunk_capacity) {
		chunk = static_cast<uint16_t>(remaining);
	} else {
		last_segment = false;
	}

	std::memcpy(payload + sdo::kSegmentHeaderSize, segment_buffer_ + segment_handled_, chunk);

	// SSC: 段头 = SDOSERVICE_UPLOADSEGMENTRES(0) | toggle | (NOMOREFOLLOWS)
	uint8_t segment_header = static_cast<uint8_t>(toggle ? sdo::kSegmentToggle : 0);

	if (last_segment) {
		segment_header = static_cast<uint8_t>(segment_header | sdo::kSegmentNoMoreFollows);

		// SSC: size < 7 时邮箱长度固定为 SEGMENT_NORM_RES_SIZE，并在段头写 (7 - size)
		if (chunk < sdo::kMinSegmentedData) {
			segment_header = static_cast<uint8_t>(segment_header | ((sdo::kMinSegmentedData - chunk) << sdo::kSegmentSizeShift));
		}
	}
	payload[0] = segment_header;

	if (last_segment) {
		segment_handled_ = 0;
		segment_command_ = 0;
		segment_total_size_ = 0;
		last_toggle_ = false;
	} else {
		segment_handled_ += chunk;
	}

	SetCoeResponse(frame);

	// SSC SdoUploadSegmentInd：末段数据不足 7 字节时，邮箱载荷固定为 SEGMENT_NORM_RES_SIZE
	// （CoE 2 + 段头 1 + 7 字节数据区），不足部分清零，否则主站按固定长度解析会错位
	uint16_t segment_data = chunk;

	if (last_segment && chunk < sdo::kMinSegmentedData) {
		segment_data = sdo::kMinSegmentedData;
		std::memset(payload + sdo::kSegmentHeaderSize + chunk, 0, static_cast<size_t>(segment_data - chunk));
	}

	const uint16_t response_length = static_cast<uint16_t>(kSdoOffset + sdo::kSegmentHeaderSize + segment_data);

	SetMailboxLength(frame, static_cast<uint16_t>(response_length - kMailboxHeaderSize));

	const bool submitted = (Reply(frame, response_length) == 0);

	if (last_segment) {
		Abort();
	}
	return submitted ? 0 : sdo::kAbortNoMoreMemory;
}

/**
 * @brief 构造中止应答
 * @param frame      报文
 * @param abort_code 中止码
 * @param index      对象索引
 * @param sub_index  子索引
 */
void EcatSdo::BuildAbort(MailboxFrame* frame, uint32_t abort_code, uint16_t index, uint8_t sub_index)
{
	uint8_t* payload = frame->data + kSdoOffset;

	// SSC SdoRes 的 abort 分支：命令字节只有 0x80（SDOSERVICE_ABORTTRANSFER），不带 size indicator
	payload[0] = static_cast<uint8_t>(sdo::kServiceAbort << sdo::kServiceShift);
	payload[1] = static_cast<uint8_t>(index & 0xFFU);
	payload[2] = static_cast<uint8_t>((index >> 8) & 0xFFU);
	payload[3] = sub_index;

	wire::WriteLe32(payload, 4, abort_code);

	// SSC 的 abort 应答把 CoE 服务设成 SDO Request，不是 SDO Response
	{
		const uint16_t coe_header = wire::ReadLe16(frame->data, kCoeOffset);
		const uint16_t updated = static_cast<uint16_t>((coe_header & ~coe::kServiceMask) | (static_cast<uint16_t>(coe::kServiceSdoRequest) << coe::kServiceShift));

		wire::WriteLe16(frame->data, kCoeOffset, updated);
	}

	SetMailboxLength(frame, 10);
}

/**
 * @brief 把应答交给邮箱层
 * @param frame  报文
 * @param length 应答字节数
 * @return 0=成功
 */
uint8_t EcatSdo::Reply(MailboxFrame* frame, uint16_t length)
{
	if (frame == nullptr) {
		return mailbox::kErrorInvalidSize;
	}

	frame->length = length;

	if (sink_ != nullptr) {
		return sink_(sink_context_, frame->data, length);
	}
	return 0;
}

/**
 * @brief 中止当前会话并清分段状态
 */
void EcatSdo::Abort()
{
	in_work_ = false;
	segment_command_ = 0;
	segment_index_ = 0;
	segment_sub_index_ = 0;
	segment_total_size_ = 0;
	segment_handled_ = 0;
	segment_complete_access_ = false;
	last_toggle_ = false;
}

}
