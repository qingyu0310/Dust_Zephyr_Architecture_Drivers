/**
 * @file ethercat.cpp
 * @author qingyu
 * @brief EtherCAT 从站顶层封装实现
 * @version 0.3
 * @date 2026-10-29
 *
 * 只做生命周期转发，不含 SSC 细节、不含硬件细节。
 *
 * @copyright Copyright (c) 2026
 */

#include "ethercat.hpp"

#include "log.hpp"

namespace ecat
{

/**
 * @brief 初始化协议栈与硬件
 * @return true=成功
 */
bool EthercatSlave::Init()
{
	if (!stack_.Init()) {
		DUST_LOG_ERR("ecat: stack init failed");
		return false;
	}
	return true;
}

/**
 * @brief 推一拍协议栈（单拍，立刻返回）
 * @return true=仍在运行；false=已停止
 */
bool EthercatSlave::Poll()
{
	return stack_.Poll();
}

/**
 * @brief 请求停止
 */
void EthercatSlave::Stop()
{
	stack_.Stop();
}

} // namespace ecat
