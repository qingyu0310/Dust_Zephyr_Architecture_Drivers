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
#include <cstring>

#include "log.hpp"

extern "C" {

#include "applInterface.h"
#include "ecatappl.h"
#include "ecatslv.h"
#include "hpm_ecat_hw.h"
#include "ecat_zephyr_port.h"

// digital_io.c 不参与编译（APPL_* 钩子由本文件实现），
// 所以这里接替它定义 _DIGITAL_IO_，让 digital_ioObjects.h 展开对象变量定义
// （sRxPDOassign / MyMbxData0x2000 等）。
// 注意不能再定义 _OBJD_：ApplicationObjDic 由 coeappl.c 定义，重复会多重定义。
#define _DIGITAL_IO_ 1
#include "digital_io.h"
#undef _DIGITAL_IO_

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

	shell::Log::SendCommandLine("ecat: get esc device");
	const struct device *esc = DEVICE_DT_GET(DT_ALIAS(ethercat0));

	shell::Log::SendCommandLine("ecat: wait esc ready");
	if (!device_is_ready(esc)) {
		shell::Log::SendCommandLine("ecat: esc not ready");
		return false;
	}
	shell::Log::SendCommandLine("ecat: esc ready");

	// 照 sample main()：先挂中断回调，再跑硬件初始化
	shell::Log::SendCommandLine("ecat: irq init");
	ecat_irq_init();

	shell::Log::SendCommandLine("ecat: hw init begin");
	if (ecat_hardware_init(esc) != 0) {
		shell::Log::SendCommandLine("ecat: hw init failed");
		return false;
	}
	shell::Log::SendCommandLine("ecat: hw init done");

	shell::Log::SendCommandLine("ecat: MainInit begin");
	if (MainInit() != 0U) {
		shell::Log::SendCommandLine("ecat: MainInit failed");
		return false;
	}
	shell::Log::SendCommandLine("ecat: MainInit done");

	// MainInit 会清掉 EEPROM 钩子，必须在它之后重新装
	shell::Log::SendCommandLine("ecat: eeprom hooks");
	pAPPL_EEPROM_Read = ecat_eeprom_emulation_read;
	pAPPL_EEPROM_Write = ecat_eeprom_emulation_write;
	pAPPL_EEPROM_Reload = ecat_eeprom_emulation_reload;
	pAPPL_EEPROM_Store = ecat_eeprom_emulation_store;

	// 算 PDO 长度（sample 里也是这一步）
	shell::Log::SendCommandLine("ecat: pdo mapping");
	if (APPL_GenerateMapping(&nPdInputSize, &nPdOutputSize) != 0U) {
		shell::Log::SendCommandLine("ecat: pdo mapping failed");
		return false;
	}

	ecat_phy_update_link(esc);
	bRunApplication = TRUE;
	hpm_esc_start(esc);
	shell::Log::SendCommandLine("ecat: slave initialised");

	initialised_ = true;
	return true;
}

/**
 * @brief 运行协议栈主循环（阻塞）
 */
void EthercatSlave::Run()
{
	if (!initialised_) {
		shell::Log::SendCommandLine("ecat: Run before Init");
		return;
	}

	const struct device* esc = DEVICE_DT_GET(DT_ALIAS(ethercat0));
	int64_t next_link_poll = 0;

	while (bRunApplication == TRUE)
	{
		MainLoop();

		const int64_t now = k_uptime_get();
		if (now >= next_link_poll) {
			ecat_phy_update_link(esc);
			next_link_poll = now + 100;
		}

		// 让出 CPU：High 优先级的纯忙循环会饿死启动器和 shell 线程
		k_usleep(100);
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

extern "C" void ecat_port_log(const char* text)
{
	shell::Log::SendCommandLine(text);
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
