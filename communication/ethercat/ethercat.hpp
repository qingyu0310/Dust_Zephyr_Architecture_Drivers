/**
 * @file ethercat.hpp
 * @author qingyu
 * @brief EtherCAT 从站驱动 — 倍福 SSC 5.13 的 C++ 封装
 * @version 0.1
 * @date 2026-10-28
 *
 * # EtherCAT 从站使用说明
 *
 * 本类把倍福 SSC（Slave Stack Code 5.13）包成与 `Can` / `UartDma` 同一形态的
 * C++ 驱动：业务侧只注册 std::function，不需要写任何 C 签名，也不需要碰 `HW_*` 宏。
 *
 * ## 分层
 *
 * ```
 * trd_ecat.cpp                 业务：注册回调 + PDO 字段映射
 *   ↓
 * EthercatSlave (本文件)       生命周期 + 三个 APPL_* 钩子转发
 *   ↓ esc_hw.h（中性契约名）
 * sdk_glue_user/drivers/ethercat/   芯片端口层（HW_* 宏、ISR、PHY、SII）
 * ```
 *
 * ## 设备树
 *
 * 板级 dts（`sdk_glue_user/boards/hpmicro/uhpm6e00evk/uhpm6e00evk.dts`）：
 * ```dts
 * / {
 *     aliases {
 *         ethercat0 = &esc0;
 *     };
 * };
 *
 * &esc0 {
 *     status = "okay";
 * };
 * ```
 *
 * ## Kconfig
 * ```kconfig
 * config TRD_ECAT
 *     select DUST_COM_ETHERCAT
 *     select HPM_ESC
 * ```
 *
 * ## 初始化与用法
 * ```cpp
 * static EthercatSlave &ecat = EthercatSlave::Instance();
 *
 * bool thread_init()
 * {
 *     if (!ecat.Init()) {
 *         return false;
 *     }
 *
 *     // 主站 → 从站（SM2 / RxPDO）：解析指令
 *     ecat.SetRxHandler([](const uint8_t *data, size_t len) {
 *         // data[0..len-1] 是主站本周期发来的字节
 *     });
 *
 *     // 从站 → 主站（SM3 / TxPDO）：填好回传数据
 *     ecat.SetTxProvider([](uint8_t *data, size_t len) {
 *         // 把状态写进 data[0..len-1]
 *     });
 *
 *     return true;
 * }
 * ```
 *
 * ## 回调语义
 *
 * | 回调 | 触发时机 | 由谁调用 | 必须做什么 |
 * |---|---|---|---|
 * | RxHandler | 主站数据到达（SM2 更新后） | SSC APPL_OutputMapping | 解析本周期输入 |
 * | TxProvider | 协议栈要发数据前（SM3 提交前） | SSC APPL_InputMapping | 填满输出缓冲 |
 * | CycleHandler | 每周期应用阶段 | SSC APPL_Application | 可选，业务节拍 |
 *
 * 三个回调都在 SSC 的协议上下文中调用，必须短、不能阻塞、不能 sleep。
 *
 * @note 本类只依赖中性契约名 esc_hw.h，不含任何厂商头文件。
 *       芯片相关实现由 sdk_glue_user/drivers/ethercat/ 提供。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

/**
 * @brief EtherCAT 从站驱动
 */
class EthercatSlave final
{
public:
	using CycleHandler = std::function<void()>;                      // 每周期业务回调
	using RxHandler = std::function<void(const uint8_t *, size_t)>;  // 主站→从站（SM2 / RxPDO）
	using TxProvider = std::function<void(uint8_t *, size_t)>;       // 从站→主站（SM3 / TxPDO）

	static constexpr size_t kRxPdoSize = 64;  // SM2 容量上限，对应 SSC MAX_PD_OUTPUT_SIZE
	static constexpr size_t kTxPdoSize = 64;  // SM3 容量上限，对应 SSC MAX_PD_INPUT_SIZE

	static EthercatSlave &Instance();

	EthercatSlave(const EthercatSlave &) = delete;
	EthercatSlave &operator=(const EthercatSlave &) = delete;

	bool Init();
	void Run();

	void SetCycleHandler(CycleHandler handler);
	void SetRxHandler(RxHandler handler);
	void SetTxProvider(TxProvider handler);

	// 以下三个仅供 SSC 的 APPL_* 钩子调用，业务请用上面的 Set* 系列
	void OnOutputMapping(uint16_t *pData);
	void OnInputMapping(uint16_t *pData);
	void OnApplication();

private:
	EthercatSlave() = default;
	~EthercatSlave() = default;

	CycleHandler cycle_handler_{};
	RxHandler rx_handler_{};
	TxProvider tx_handler_{};
	bool initialised_ = false;  // Init() 是否已完成
};
