/**
 * @file ecat_limits.hpp
 * @author qingyu
 * @brief 协议容量、超时、身份标识常量，全部编译期求值
 * @version 0.1
 * @date 2026-10-30
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace ecat {

/* 过程数据容量（每方向），容量上限不是实际有效长度——实际长度由 0x1C12/0x1C13 算出 */
constexpr uint16_t kMaxProcessInputSize  = 0x40;	// 从站→主站（SM3 / TxPDO）容量
constexpr uint16_t kMaxProcessOutputSize = 0x40;	// 主站→从站（SM2 / RxPDO）容量

/* 邮箱尺寸：SM0 按最小尺寸、SM1 按最大尺寸，这个不对称是 SSC 的既有行为 */
constexpr uint16_t kMinMailboxSize = 0x0024;		// SM0 接收邮箱尺寸
constexpr uint16_t kMaxMailboxSize = 0x0080;		// SM1 发送邮箱尺寸

constexpr uint16_t kMailboxHeaderSize = 6;					// 长度 2 B + 地址 2 B + 标志 2 B
constexpr uint16_t kMailboxDataSize   = kMaxMailboxSize - kMailboxHeaderSize;	// 邮箱载荷上限

/* 队列数组实际长度是深度 + 1，环形队列留一个空位区分满与空 */
constexpr uint16_t kMailboxQueueDepth = 10;					// 邮箱报文队列深度

/* 同时存活的缓冲：写/读/重发/存储各 1 + 收发两个队列 */
constexpr uint16_t kMailboxBufferCount = 4 + 2 * kMailboxQueueDepth;		// 静态池容量

constexpr uint8_t kMaxSyncManagerCount = 16;	// ESC 支持的 SM 通道上限
constexpr uint8_t kSyncManagerRegSize  = 8;	// 每个 SM 占 8 B 配置/状态寄存器

/* SM 地址范围，照 SSC ecat_def.h：邮箱与过程数据都必须落在 DPRAM 用户区 0x1000~0x2FFF */
constexpr uint16_t kMinMbxWriteAddress = 0x1000;	// SM0 接收邮箱地址下界
constexpr uint16_t kMaxMbxWriteAddress = 0x2FFF;	// SM0 接收邮箱地址上界
constexpr uint16_t kMinMbxReadAddress  = 0x1000;	// SM1 发送邮箱地址下界
constexpr uint16_t kMaxMbxReadAddress  = 0x2FFF;	// SM1 发送邮箱地址上界
constexpr uint16_t kMinPdWriteAddress  = 0x1000;	// SM2 输出过程数据地址下界
constexpr uint16_t kMaxPdWriteAddress  = 0x2FFF;	// SM2 输出过程数据地址上界
constexpr uint16_t kMinPdReadAddress   = 0x1000;	// SM3 输入过程数据地址下界
constexpr uint16_t kMaxPdReadAddress   = 0x2FFF;	// SM3 输入过程数据地址上界

/* 状态机超时原值，实际等待时间见下面的 kEnter*TimeoutMs */
constexpr uint16_t kPreOpTimeoutRaw      = 0x07D0;	// PREOPTIMEOUT = 2000 ms
constexpr uint16_t kSafeOpToOpTimeoutRaw = 0x2328;	// SAFEOP2OPTIMEOUT = 9000 ms
constexpr uint16_t kDefaultTimeoutRaw    = 200;		// 其他转换的默认超时

/**
 * @brief 扣除 10% 之后的状态转换等待时间
 *
 * SSC 装载计数时还会 `EsmTimeoutCounter -= EsmTimeoutCounter / 10`，
 * 即再扣 10% 提前反应，不要把裸值当最终等待时间。
 *
 * @param raw 原始超时值（ms）
 * @return 实际生效的等待时间（ms）
 */
constexpr uint16_t EffectiveTimeout(uint16_t raw)
{
	return static_cast<uint16_t>(raw - raw / 10);
}

constexpr uint16_t kEnterPreOpTimeoutMs = EffectiveTimeout(kPreOpTimeoutRaw);		// 1800 ms
constexpr uint16_t kSafeOpToOpTimeoutMs = EffectiveTimeout(kSafeOpToOpTimeoutRaw);	// 8100 ms
constexpr uint16_t kDefaultTimeoutMs    = EffectiveTimeout(kDefaultTimeoutRaw);		// 180 ms

/* 同步与时基 */
constexpr uint32_t kMinProcessCycleTimeNs = 0x79E0;			// 0x1C3x:05 支持的最小周期 = 31 200 ns
constexpr uint32_t kMaxProcessCycleTimeNs = 0xC3500000;		// 支持的最大周期 = 3.2768 s
constexpr uint32_t kOutputDelayTimeNs     = 0x0;			// 0x1C32:09
constexpr uint32_t kInputDelayTimeNs      = 0x0;			// 0x1C33:09
constexpr uint32_t kOutputCalcCopyTimeNs  = 0x0;			// 0x1C32:06，0 表示由从站实测
constexpr uint32_t kInputCalcCopyTimeNs   = 0x0;			// 0x1C33:06，0 表示由从站实测

constexpr uint32_t kCheckDcOverrunMs = 4000;				// DC 时间戳溢出检查周期
constexpr uint32_t kLinkPollMs       = 100;					// 链路刷新周期

/* 主站写完 SII 后等 1 s 无新写入才落盘，避免逐字擦写 */
constexpr uint16_t kSiiStoreDelayMs = 1000;					// SII 落盘延迟
constexpr uint16_t kSiiWriteSize    = 8;					// SSC EEPROM_WRITE_SIZE：写命令一次搬 8 字节
constexpr uint32_t kSiiSizeBytes    = 0x800;				// SII 存储容量 2048 B

/* 身份标识，改动等于换设备型号 */
constexpr uint32_t kVendorId       = 0x0048504D;			// 0x1018:01，"HPM" 的 ASCII
constexpr uint32_t kProductCode    = 0x00000001;			// 0x1018:02
constexpr uint32_t kRevisionNumber = 0x00000001;			// 0x1018:03
constexpr uint32_t kSerialNumber   = 0x00000000;			// 0x1018:04，0 = 运行时由主站分配

constexpr char kDeviceName[] = "digital_io";				// 0x1008，长度不含结尾 '\0'
constexpr uint8_t kDeviceNameLength = 0x0A;
constexpr char kHardwareVersion[] = "ECAT_Device";			// 0x1009
constexpr uint8_t kHardwareVersionLength = 0x0B;
constexpr char kSoftwareVersion[] = "1.0";					// 0x100A
constexpr uint8_t kSoftwareVersionLength = 0x03;

/* 编译期能力声明，不是运行期配置——改动会改变协议栈暴露给主站的坐标系 */
constexpr bool kCoESupported        = true;					// 支持 CoE（SDO / SDO Info）
constexpr bool kDcSupported         = true;					// 支持分布式时钟
constexpr bool kAlEventEnabled      = true;					// 使能 AL Event 中断
constexpr bool kMailboxQueueEnabled = true;					// 邮箱服务排队
constexpr bool kDc32BitSupported    = true;					// DC 单位是否为 32 位（由 0x0008.3 决定，见 EcatSync）

/**
 * @brief 位长度转字节长度
 * @param bits 位长度
 * @return 字节长度（向上取整）
 */
constexpr uint16_t BitsToBytes(uint32_t bits)
{
	return static_cast<uint16_t>((bits + 7U) / 8U);
}

/**
 * @brief 字节长度转位长度
 * @param bytes 字节长度
 * @return 位长度
 */
constexpr uint16_t BytesToBits(uint16_t bytes)
{
	return static_cast<uint16_t>(bytes * 8U);
}

}
