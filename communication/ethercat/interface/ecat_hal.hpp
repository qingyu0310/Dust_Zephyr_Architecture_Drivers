/**
 * @file ecat_hal.hpp
 * @author qingyu
 * @brief EtherCAT 硬件抽象层 — 纯虚基类，不依赖任何平台
 * @version 0.1
 * @date 2026-10-29
 *
 * EcatHal 是 EtherCAT 栈调用硬件的唯一接口。
 * 具体 MCU 通过继承此类实现：EcatHalHpm（5E00/6E00）等。
 *
 * 职责边界：
 *   - 必须处理：ESC 寄存器读写、MDIO、中断、PHY 复位/链路、EEPROM 时序、临界区
 *   - 不得处理：AL 状态机、PDO 映射、邮箱协议、SII 内容、具体 PHY 芯片逻辑
 *
 * HAL 不知道 EthercatSlave、EcatStack，只负责硬件访问与事件上报。
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "ecat_types.hpp"

/**
 * @brief EtherCAT 硬件抽象层
 */
class EcatHal
{
public:
	// 事件回调（中断转发）
	using EventCallback = void (*)(void* context, EcatIrq irq);

	/**
	 * @brief HAL 配置
	 */
	struct Config
	{
		uint8_t port_count = 2;             // 使能端口数（1~3）
	};

	virtual ~EcatHal() = default;

	/**
	 * @brief 初始化 ESC 硬件（时钟/引脚/复位/EEPROM 模式/PHY）
	 * @param cfg       硬件配置
	 * @param callback  中断回调
	 * @param context   回调上下文
	 * @return true=成功
	 */
	virtual bool Init(const Config& cfg, EventCallback callback, void* context) = 0;

	/**
	 * @brief 启动 ESC 状态机
	 */
	virtual bool Start() = 0;

	/**
	 * @brief 停止 ESC
	 */
	virtual void Stop() = 0;

	/**
	 * @brief 读 ESC 寄存器/DPRAM
	 * @param addr  偏移
	 * @param buf   目标缓冲
	 * @param len   字节数
	 */
	virtual void Read(uint16_t addr, void* buf, uint16_t len) = 0;

	/**
	 * @brief 写 ESC 寄存器/DPRAM
	 */
	virtual void Write(uint16_t addr, const void* buf, uint16_t len) = 0;

	/**
	 * @brief 读 AL Event Request 寄存器（0x0220）
	 */
	virtual uint16_t AlEvent() = 0;

	/**
	 * @brief MDIO 读（PHY 寄存器）
	 */
	virtual bool MdioRead(uint8_t phy, uint8_t reg, uint16_t* value) = 0;

	/**
	 * @brief MDIO 写
	 */
	virtual bool MdioWrite(uint8_t phy, uint8_t reg, uint16_t value) = 0;

	/**
	 * @brief 设置端口链路状态（软件链路模式写 GPR_CFG2）
	 * @param port  端口号 0~2
	 * @param up    true=link up
	 */
	virtual void SetLink(uint8_t port, bool up) = 0;

	/**
	 * @brief 轮询各端口 PHY 的 link 状态并写入 ESC
	 *
	 * 应用需周期性调用（建议 100ms）。
	 */
	virtual void UpdateLink() = 0;

	/**
	 * @brief 读 SII 一个字
	 */
	virtual uint16_t EepromRead(uint32_t word_addr) = 0;

	/**
	 * @brief 写 SII 一个字
	 */
	virtual void EepromWrite(uint32_t word_addr, uint16_t data) = 0;

	/**
	 * @brief SII reload 应答
	 */
	virtual uint32_t EepromReload() = 0;

	/**
	 * @brief 取毫秒计时（SSC 的 HW_GetTimer）
	 */
	virtual uint32_t TimerGet() = 0;

	/**
	 * @brief 清零计时基准
	 */
	virtual void TimerClear() = 0;

	/**
	 * @brief 进入临界区（SSC 的 DISABLE_ESC_INT）
	 */
	virtual void EnterCritical() = 0;

	/**
	 * @brief 离开临界区
	 */
	virtual void LeaveCritical() = 0;
};

/**
 * @brief 获取默认 HAL 实例
 *
 * 由各平台 HAL 实现提供（如 ecat_hal_hpm.cpp）。
 * 用户不需要手动创建 HAL 实例。
 */
EcatHal& GetDefaultEcatHal();
