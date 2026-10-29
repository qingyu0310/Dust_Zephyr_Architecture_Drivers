/**
 * @file ecat_appl_hooks.cpp
 * @author qingyu
 * @brief 倍福 SSC 应用接口钩子 — 全工程唯一出现 extern "C" APPL_* 的地方
 * @version 0.1
 * @date 2026-10-29
 *
 * 每个钩子只做一件事：转发到 EcatStack::Instance()。不含任何逻辑。
 *
 * @copyright Copyright (c) 2026
 */

#include "core/ecat_pdo.hpp"
#include "core/ecat_stack.hpp"
#include "log.hpp"

extern "C" {

#include "applInterface.h"
#include "ecatappl.h"

/**
 * @brief 主站→从站过程数据已更新
 * @param pData SM2 首地址
 */
void APPL_OutputMapping(uint16_t* pData)
{
	EcatStack::Instance().OnOutputMapping(pData);
}

/**
 * @brief 从站→主站过程数据提交前
 * @param pData SM3 首地址
 */
void APPL_InputMapping(uint16_t* pData)
{
	EcatStack::Instance().OnInputMapping(pData);
}

/**
 * @brief 每周期应用阶段
 */
void APPL_Application(void)
{
	EcatStack::Instance().OnApplication();
}

void APPL_AckErrorInd(uint16_t stateTrans)
{
	(void)stateTrans;
}

uint16_t APPL_StartMailboxHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

uint16_t APPL_StopMailboxHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

uint16_t APPL_StartInputHandler(uint16_t* pIntMask)
{
	(void)pIntMask;
	return ALSTATUSCODE_NOERROR;
}

uint16_t APPL_StopInputHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

uint16_t APPL_StartOutputHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

uint16_t APPL_StopOutputHandler(void)
{
	return ALSTATUSCODE_NOERROR;
}

uint16_t APPL_GetDeviceID(void)
{
	return 0U;
}

/**
 * @brief 按 0x1C12 / 0x1C13 的实际映射计算过程数据长度
 * @param pInputSize  SM3（TxPDO）长度输出
 * @param pOutputSize SM2（RxPDO）长度输出
 * @return AL 状态码
 */
uint16_t APPL_GenerateMapping(uint16_t* pInputSize, uint16_t* pOutputSize)
{
	const PdoLayout layout = EcatPdo::Generate();

	*pInputSize  = layout.tx_size;
	*pOutputSize = layout.rx_size;
	return ALSTATUSCODE_NOERROR;
}

/**
 * @brief 端口层日志转发（供 HAL 的 C 文件调用）
 * @param text 以 '\0' 结尾的字符串
 */
void ecat_port_log(const char* text)
{
	shell::Log::SendCommandLine(text);
}

} // extern "C"
