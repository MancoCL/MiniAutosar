/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Common.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境的公共基础头（类型、E_OK/E_NOT_OK、内存辅助函数）。
 **
 **  @details            : 目标工程中 Common.h 由集成方提供，声明 uint8/uint32、E_OK/E_NOT_OK、NULL_PTR 以及
 **                        CommF_DataSet / CommF_DataCopy 等辅助接口。本文件为 PC 侧最小替代实现，语义保持兼容：
 **                        1) CommF_DataSet(dst, value, len) ：把 dst 起 len 字节全部置为 value；
 **                        2) CommF_DataCopy(dst, src, len)  ：把 src 起 len 字节拷贝到 dst。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。
 **
 ***********************************************************************************************************************/

#ifndef COMMON_H_
#define COMMON_H_

#include "Std_Types.h"

/* ========================================== external function declarations ========================================= */
/**
 * @brief           把 dst 起 length 字节全部置为 value。
 * @param[out]      dst: 目标缓冲。
 * @param[in]       value: 填充值。
 * @param[in]       length: 字节数。
 * @return          void
 */
extern void CommF_DataSet(void* dst, uint8 value, uint32 length);

/**
 * @brief           把 src 起 length 字节拷贝到 dst（不处理重叠）。
 * @param[out]      dst: 目标缓冲。
 * @param[in]       src: 源缓冲。
 * @param[in]       length: 字节数。
 * @return          void
 */
extern void CommF_DataCopy(void* dst, const void* src, uint32 length);

#endif /* COMMON_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
