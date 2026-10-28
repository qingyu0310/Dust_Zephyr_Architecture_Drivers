/**
 * @file ethercat.cpp
 * @author qingyu
 * @brief EtherCAT 从站驱动实现 — 倍福 SSC 5.13 钩子转发
 * @version 0.2
 * @date 2026-10-28
 */

#include "ethercat.hpp"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include "log.hpp"

extern "C" {

#include "applInterface.h"
#include "ecat_zephyr_port.h"

// 让 digital_ioObjects.h 展开对象字典实例 ApplicationObjDic
#define _OBJD_
#include "digital_io.h"

}

#pragma message "Compiling Drivers/Communication EtherCAT"

EthercatSlave &EthercatSlave::Instance()
{
	static EthercatSlave instance;
	return instance;
}

/**
 * @brief 初始化 ESC 硬件与协议栈
 * @return true=成功
 */
bool EthercatSlave::Init()
{
	if (initialised_) {
		return true;
	}

	const struct device *esc = DEVICE_DT_GET(DT_ALIAS(ethercat0));
	if (!device_is_ready(esc)) {
		DUST_LOG_ERR("ethercat esc not ready");
		return false;
	}

	// 先把 PDI/SYNC/RESET 中断回调接到 ESC 设备，再启动协议栈
	ecat_irq_init();

	// 协议栈内部会调用 HW_Init() 完成 ESC 硬件初始化
	if (MainInit() != 0U) {
		DUST_LOG_ERR("ethercat MainInit failed");
		return false;
	}

	initialised_ = true;
	DUST_LOG_OK("ethercat slave initialised");
	return true;
}

/**
 * @brief 运行协议栈主循环（阻塞）
 */
void EthercatSlave::Run()
{
	if (!initialised_) {
		DUST_LOG_ERR("ethercat Run() before Init()");
		return;
	}

	for (;;) {
		MainLoop();
	}
}

void EthercatSlave::SetCycleHandler(CycleHandler handler)
{
	cycle_handler_ = std::move(handler);
}

void EthercatSlave::SetRxHandler(RxHandler handler)
{
	rx_handler_ = std::move(handler);
}

void EthercatSlave::SetTxProvider(TxProvider handler)
{
	tx_handler_ = std::move(handler);
}

/**
 * @brief SM2 已由协议栈读入，转交业务解析
 * @param pData SM2 过程数据首地址
 */
void EthercatSlave::OnOutputMapping(uint16_t *pData)
{
	if (rx_handler_) {
		rx_handler_(reinterpret_cast<const uint8_t *>(pData), kRxPdoSize);
	}
}

/**
 * @brief 业务把本周期输出填进 SM3 缓冲
 * @param pData SM3 过程数据首地址
 */
void EthercatSlave::OnInputMapping(uint16_t *pData)
{
	if (tx_handler_) {
		tx_handler_(reinterpret_cast<uint8_t *>(pData), kTxPdoSize);
	}
}

void EthercatSlave::OnApplication()
{
	if (cycle_handler_) {
		cycle_handler_();
	}
}

// 下面这一组是倍福 SSC 要求的应用接口，全工程只在这里出现一次

extern "C" void APPL_OutputMapping(uint16_t *pData)
{
	EthercatSlave::Instance().OnOutputMapping(pData);
}

extern "C" void APPL_InputMapping(uint16_t *pData)
{
	EthercatSlave::Instance().OnInputMapping(pData);
}

extern "C" void APPL_Application(void)
{
	EthercatSlave::Instance().OnApplication();
}

extern "C" void APPL_AckErrorInd(uint16_t stateTrans)
{
	(void)stateTrans;
}

extern "C" uint16_t APPL_StartMailboxHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

extern "C" uint16_t APPL_StopMailboxHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

extern "C" uint16_t APPL_StartInputHandler(uint16_t *pIntMask)
{
	(void)pIntMask;
	return ALSTATUSCODE_NOERROR;
}

extern "C" uint16_t APPL_StopInputHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

extern "C" uint16_t APPL_StartOutputHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

extern "C" uint16_t APPL_StopOutputHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

extern "C" uint16_t APPL_GetDeviceID(void)
{
	return 0U;
}

/**
 * @brief 按 0x1C12 / 0x1C13 的实际映射计算过程数据长度
 * @param pInputSize  SM3（TxPDO）长度输出
 * @param pOutputSize SM2（RxPDO）长度输出
 * @return AL 状态码
 */
extern "C" uint16_t APPL_GenerateMapping(uint16_t *pInputSize, uint16_t *pOutputSize)
{
	uint16_t result = ALSTATUSCODE_NOERROR;
	uint16_t input_size = 0;
	uint16_t output_size = 0;

#if COE_SUPPORTED
	uint16_t assign_cnt = 0;
	OBJCONST TOBJECT OBJMEM *p_pdo = NULL;
	uint16_t pdo_subindex0 = 0;
	uint32_t *p_pdo_entry = NULL;
	uint16_t pdo_entry_cnt = 0;

#if MAX_PD_OUTPUT_SIZE > 0
	// 扫描 0x1C12 RxPDO 分配
	for (assign_cnt = 0; assign_cnt < sRxPDOassign.u16SubIndex0; assign_cnt++) {
		p_pdo = OBJ_GetObjectHandle(sRxPDOassign.aEntries[assign_cnt]);
		if (p_pdo != NULL) {
			pdo_subindex0 = *((uint16_t *)p_pdo->pVarPtr);
			for (pdo_entry_cnt = 0; pdo_entry_cnt < pdo_subindex0; pdo_entry_cnt++) {
				p_pdo_entry = (uint32_t *)(((uint16_t *)p_pdo->pVarPtr) +
					(OBJ_GetEntryOffset((pdo_entry_cnt + 1), p_pdo) >> 4));
				output_size += (uint16_t)((*p_pdo_entry) & 0xFF);
			}
		} else {
			output_size = 0;
			result = ALSTATUSCODE_INVALIDOUTPUTMAPPING;
			break;
		}
	}
	output_size = (output_size + 7) >> 3;
#endif

#if MAX_PD_INPUT_SIZE > 0
	if (result == 0) {
		// 扫描 0x1C13 TxPDO 分配
		for (assign_cnt = 0; assign_cnt < sTxPDOassign.u16SubIndex0; assign_cnt++) {
			p_pdo = OBJ_GetObjectHandle(sTxPDOassign.aEntries[assign_cnt]);
			if (p_pdo != NULL) {
				pdo_subindex0 = *((uint16_t *)p_pdo->pVarPtr);
				for (pdo_entry_cnt = 0; pdo_entry_cnt < pdo_subindex0; pdo_entry_cnt++) {
					p_pdo_entry = (uint32_t *)(((uint16_t *)p_pdo->pVarPtr) +
						(OBJ_GetEntryOffset((pdo_entry_cnt + 1), p_pdo) >> 4));
					input_size += (uint16_t)((*p_pdo_entry) & 0xFF);
				}
			} else {
				input_size = 0;
				result = ALSTATUSCODE_INVALIDINPUTMAPPING;
				break;
			}
		}
	}
	input_size = (input_size + 7) >> 3;
#endif
#endif

	*pInputSize = input_size;
	*pOutputSize = output_size;
	return result;
}
