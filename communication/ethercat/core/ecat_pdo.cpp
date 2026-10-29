/**
 * @file ecat_pdo.cpp
 * @author qingyu
 * @brief PDO 映射长度计算实现
 * @version 0.1
 * @date 2026-10-29
 *
 * @copyright Copyright (c) 2026
 */

#include "ecat_pdo.hpp"

extern "C" {

#include "ecatappl.h"
#include "ecatslv.h"
#include "objdef.h"

// digital_io.c 不参与编译（APPL_* 钩子由 appl/ 实现），
// 这里接替它定义 _DIGITAL_IO_，让 digital_ioObjects.h 展开对象变量定义
// （sRxPDOassign / sTxPDOassign / MyMbxData0x2000 等）。
// 注意不能再定义 _OBJD_：ApplicationObjDic 由 coeappl.c 定义，重复会多重定义。
#define _DIGITAL_IO_ 1
#include "digital_io.h"
#undef _DIGITAL_IO_

}

namespace
{
	/**
	 * @brief 累加一个 PDO 分配表里所有条目的位长度
	 * @param p_assign  0x1C12 或 0x1C13 分配结构
	 * @param count     分配条目数
	 * @param bits      输出：累计位长度
	 * @return true=全部条目都能解析
	 */
	bool SumPdoBits(const void* p_assign, uint16_t count, uint16_t* bits)
	{
		const TOBJ1C12* assign = static_cast<const TOBJ1C12*>(p_assign);

		*bits = 0;
		for (uint16_t i = 0; i < count; i++) {
			OBJCONST TOBJECT OBJMEM* pdo = OBJ_GetObjectHandle(assign->aEntries[i]);

			if (pdo == nullptr) {
				return false;
			}
			const uint16_t sub_count = *static_cast<uint16_t*>(pdo->pVarPtr);

			for (uint16_t entry = 0; entry < sub_count; entry++) {
				const uint32_t* p_entry = reinterpret_cast<const uint32_t*>(
					static_cast<uint16_t*>(pdo->pVarPtr) +
					(OBJ_GetEntryOffset((entry + 1), pdo) >> 4));

				*bits = static_cast<uint16_t>(*bits + (*p_entry & 0xFF));
			}
		}
		return true;
	}
}

/**
 * @brief 扫描对象字典算过程数据长度
 * @return 长度；两者都是 0 表示映射失败
 */
PdoLayout EcatPdo::Generate()
{
	PdoLayout layout {};

#if COE_SUPPORTED
#if MAX_PD_OUTPUT_SIZE > 0
	uint16_t out_bits = 0;

	if (SumPdoBits(&sRxPDOassign, sRxPDOassign.u16SubIndex0, &out_bits)) {
		layout.rx_size = static_cast<uint16_t>((out_bits + 7U) >> 3);
	}
#endif

#if MAX_PD_INPUT_SIZE > 0
	uint16_t in_bits = 0;

	if (SumPdoBits(&sTxPDOassign, sTxPDOassign.u16SubIndex0, &in_bits)) {
		layout.tx_size = static_cast<uint16_t>((in_bits + 7U) >> 3);
	}
#endif
#endif

	/* 协议栈要用这两个全局量，必须同步 */
	nPdInputSize  = layout.tx_size;
	nPdOutputSize = layout.rx_size;

	return layout;
}
