/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MemIf_Types.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境的最小 MemIf 类型定义（状态与任务结果）。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。枚举取值保持 AUTOSAR 语义。
 **
 ***********************************************************************************************************************/

#ifndef MEMIF_TYPES_H_
#define MEMIF_TYPES_H_

#include "Std_Types.h"

/**
 * @brief           底层存储驱动状态。
 */
typedef enum
{
    MEMIF_UNINIT = 0,          /**< 未初始化 */
    MEMIF_IDLE,                /**< 空闲 */
    MEMIF_BUSY,                /**< 忙 */
    MEMIF_BUSY_INTERNAL        /**< 内部忙 */
} MemIf_StatusType;

/**
 * @brief           最近一次任务结果。
 */
typedef enum
{
    MEMIF_JOB_OK = 0,          /**< 成功 */
    MEMIF_JOB_FAILED,          /**< 失败 */
    MEMIF_JOB_PENDING,         /**< 进行中 */
    MEMIF_JOB_CANCELED         /**< 已取消 */
} MemIf_JobResultType;

#endif /* MEMIF_TYPES_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
