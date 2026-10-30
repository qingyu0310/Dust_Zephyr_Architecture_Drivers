/**
 * @file ecat_registers.hpp
 * @author qingyu
 * @brief ESC 标准寄存器偏移与位掩码
 * @version 0.1
 * @date 2026-10-30
 *
 * 只有 ETG.1000 定义的通用 ESC 布局，不含任何厂商扩展寄存器。
 * 每条标注访问宽度与"读取是否有确认副作用"——SSC 里 ReadDWord/ReadWord
 * 混用、0x0120 和 0x0220 读即清事件，这两处最容易踩。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace ecat {

// ESC 信息与通信能力
namespace reg {

constexpr uint16_t kInfo         = 0x0000;				// 类型/版本/端口数，读 32 b
constexpr uint16_t kCommInfo     = 0x0004;				// SM 通道数与 DPRAM 大小，读 32 b
constexpr uint16_t kFeatures     = 0x0008;				// ESC 特性位，读 32 b
constexpr uint16_t kSlaveAddress = 0x0010;				// 站点地址，读 16 b

}

namespace comm_info {

constexpr uint32_t kSmChannelsMask  = 0x0000FF00;		// SM 通道数字段
constexpr uint8_t  kSmChannelsShift = 8;
constexpr uint32_t kDpramSizeMask   = 0x00FF0000;		// DPRAM 大小字段（KB）
constexpr uint8_t  kDpramSizeShift  = 16;

}

namespace features {

constexpr uint32_t kDc32BitMask = 0x00000008;			// 0x0008.3：DC 单位为 32 位（本板为 0 = 64 位）

}

// 状态机寄存器。kAlControl 与 kAlEvent 都是读即确认，提前读一次就把事件吃掉
namespace reg {

constexpr uint16_t kAlControl        = 0x0120;			// 主站写目标状态；读即确认 AL Control 事件，读 32 b
constexpr uint16_t kAlStatus         = 0x0130;			// 当前状态（低四位）+ 错误标志位，读 16 b
constexpr uint16_t kAlStatusCode     = 0x0134;			// AL 状态码，读 16 b
constexpr uint16_t kRunLedOverride   = 0x0138;			// RUN 灯覆盖，写 8 b
constexpr uint16_t kErrorLedOverride = 0x0139;			// ERROR 灯覆盖，写 8 b
constexpr uint16_t kPdiControl       = 0x0140;			// PDI 类型，读 32 b
constexpr uint16_t kPdiConfig        = 0x0150;			// PDI 配置，读 32 b
constexpr uint16_t kAlEventMask      = 0x0204;			// AL 事件掩码，决定哪些事件触发 PDI 中断，读 32 b
constexpr uint16_t kAlEvent          = 0x0220;			// AL 事件请求；读即确认，读 16 b

}

// 看门狗
namespace reg {

constexpr uint16_t kWdDivider = 0x0400;					// 看门狗基准分频，读 16 b
constexpr uint16_t kPdWdTime  = 0x0420;					// 过程数据看门狗时间，读 16 b
constexpr uint16_t kPdWdState = 0x0440;					// 过程数据看门狗状态，读 32 b

}

constexpr uint32_t kPdWdTriggerMask = 0x00000001;		// 0x0440.0：SM2 看门狗触发位

// SII 访问。0x0500 是一个 32 位寄存器，低 16 配置、高 16 命令/状态
namespace reg {

constexpr uint16_t kEepromConfig  = 0x0500;				// 低 16 b 配置 + 高 16 b 控制，读 32 b
constexpr uint16_t kEepromAddress = 0x0504;				// SII 字地址，读 32 b
constexpr uint16_t kEepromData    = 0x0508;				// SII 数据窗口，读 32 b

}

namespace eeprom_config {

constexpr uint32_t kConfigMask         = 0x0000FFFF;	// 低 16 位配置字段
constexpr uint32_t kAssignToPdiMask    = 0x00000001;	// 0x0500.0：PDI 接管 EEPROM 访问
constexpr uint32_t kLockedByPdiMask    = 0x00000100;	// 0x0500.8：PDI 已锁定 EEPROM
constexpr uint32_t kControlMask        = 0xFFFF0000;	// 高 16 位控制/状态字段
constexpr uint8_t  kControlShift       = 16;
constexpr uint32_t kSupportedReadBytes = 0x00400000;	// 0x0502.6：1 = 支持 8 字节读，0 = 4 字节
constexpr uint32_t kCmdMask            = 0x07000000;	// 0x0502.8~10：命令位
constexpr uint32_t kCmdRead            = 0x01000000;	// 0x0502.8：正在执行读
constexpr uint32_t kCmdWrite           = 0x02000000;	// 0x0502.9：初始化写命令
constexpr uint32_t kCmdReload          = 0x04000000;	// 0x0502.10：触发 SII 重载
constexpr uint32_t kErrorMask          = 0x78000000;	// 所有错误位
constexpr uint32_t kErrorCrc           = 0x08000000;	// 0x0502.11：CRC 错误
constexpr uint32_t kErrorLoad          = 0x10000000;	// 0x0502.12：加载失败（0 = 正常）
constexpr uint32_t kErrorCmdAck        = 0x20000000;	// 0x0502.13：应答缺失
constexpr uint32_t kBusyMask           = 0x80000000;	// 0x0502.15：忙

}

// 同步管理器。每个 SM 占 8 B，通道 n 的基址 = kSyncManagerBase + n * kSyncManagerRegSize
namespace reg {

constexpr uint16_t kSyncManagerBase = 0x0800;	// SM 配置区起始

// 每个 SM 的 8 字节布局：+0 地址长度(4B) / +4 控制 / +5 状态 / +6 使能 / +7 PDI 控制
constexpr uint16_t kSyncManagerEnableOffset = 6;	// 0x0806：使能字节，读它清除 SM_ACT

}

namespace sync_manager_reg {

constexpr uint32_t kAddressMask = 0x0000FFFF;			// 地址字段
constexpr uint32_t kLengthMask  = 0xFFFF0000;			// 长度字段
constexpr uint8_t  kLengthShift = 16;

// 0x0804 控制寄存器
constexpr uint32_t kControlMask     = 0x0000FFFF;		// 低 16 位控制
constexpr uint32_t kModeMask        = 0x00000002;		// 缓冲模式位
constexpr uint32_t kModeThreeBuffer = 0x00000000;		// 三缓冲模式
constexpr uint32_t kModeOneBuffer   = 0x00000002;		// 单缓冲模式
constexpr uint32_t kDirectionMask   = 0x0000000C;		// 方向位
constexpr uint32_t kDirectionRead   = 0x00000000;		// 读方向（SM3）
constexpr uint32_t kDirectionWrite  = 0x00000004;		// 写方向（SM2）
constexpr uint32_t kWatchdogEnable  = 0x00000040;		// 该通道启用看门狗

// SSC SM_STATUS_MBX_BUFFER_FULL = 0x00000800（状态字节 bit3，对应 dword bit11）
constexpr uint32_t kStatusMbxFull = 0x00000800;			// 单缓冲模式下邮箱已被完整写入

// 0x0806 使能寄存器
constexpr uint32_t kEnableValue        = 0x00010000;	// 0x0806.0：使能该通道
constexpr uint8_t  kEnableShift        = 16;
constexpr uint32_t kRepeatRequestMask  = 0x00020000;	// 0x0806.1：主站请求重发
constexpr uint8_t  kRepeatRequestShift = 16;

// 0x0807 PDI 控制寄存器
constexpr uint32_t kPdiDisable = 0x01000000;			// 0x0807.0：PDI 禁用该通道
constexpr uint32_t kRepeatAck  = 0x02000000;			// 0x0807.1：重发已确认

}

// 分布式时钟
namespace reg {

constexpr uint16_t kSystemTime       = 0x0910;			// 系统时间副本，读 32 b
constexpr uint16_t kDcUnitControl    = 0x0980;			// DC 控制/激活，读 32 b
constexpr uint16_t kDcSyncStatus     = 0x098C;			// Sync0/Sync1 状态；读即确认 SYNC 事件，读 32 b
constexpr uint16_t kDcSync0CycleTime = 0x09A0;			// Sync0 周期（ns），读 32 b
constexpr uint16_t kDcSync1CycleTime = 0x09A4;			// Sync1 周期（ns），读 32 b

}

namespace dc_control {

constexpr uint32_t kSyncUnitActive     = 0x00000100;	// 0x0980.8：同步输出单元已激活
constexpr uint32_t kSync0Active        = 0x00000200;	// 0x0980.9：正在产生 SYNC0
constexpr uint32_t kSync1Active        = 0x00000400;	// 0x0980.10：正在产生 SYNC1
constexpr uint32_t kSyncUnitAutoActive = 0x00000800;	// 0x0980.11：写系统时间时自动激活
constexpr uint32_t kActivationMask     = 0x0000FFFF;	// 激活位整体掩码

}

constexpr uint16_t kDpramStartAddress 	 = 0x1000;		// DPRAM 起始地址
constexpr uint16_t kBlockPageSize     	 = 512;			// 分块传输页大小
constexpr uint32_t kDeviceEmulation   	 = 0x00000100;	// 0x0140.8：设备仿真模式
constexpr uint32_t kSync0MappedToAlEvent = 0x00000800;	// 0x0150：SYNC0 映射到 AL 事件
constexpr uint32_t kSync1MappedToAlEvent = 0x00008000;	// 0x0150：SYNC1 映射到 AL 事件

}
