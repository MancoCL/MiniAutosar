/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Fls_Sim.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机 Fls 仿真的调试/自检辅助接口（非 AUTOSAR 接口）。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。
 **
 ***********************************************************************************************************************/

#ifndef FLS_SIM_H_
#define FLS_SIM_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"

/* ============================================ external data declarations =========================================== */
/**
 * @brief           仿真 Flash 后备 RAM 基址（按 Fls 偏移访问，即 offset 0 对应此处）。
 */
extern uint8 Fls_Sim_Memory[];

/**
 * @brief           仿真 Flash 总大小（字节）。
 */
extern const uint32 Fls_Sim_Size;

/**
 * @brief           二次编程违规计数（写请求试图把 0 位改回 1，或写入已编程区域）。
 */
extern uint32 Fls_Sim_ProgramViolations;

/**
 * @brief           越界访问计数（偏移 + 长度超出仿真 Flash 范围）。
 */
extern uint32 Fls_Sim_OutOfRange;

/**
 * @brief           驱动拒绝的请求计数（未初始化等）。
 */
extern uint32 Fls_Sim_RejectedRequests;

/* ========================================== external function declarations ========================================= */
/**
 * @brief           把仿真 Flash 全部复位为擦除态（0xFF），并清零统计。
 * @return          void
 */
extern void Fls_Sim_Reset(void);

/**
 * @brief           dump 仿真 Flash 指定范围（调试用，输出到 stdout）。
 * @param[in]       offset: 起始偏移。
 * @param[in]       length: 字节数。
 * @return          void
 */
extern void Fls_Sim_Dump(uint32 offset, uint32 length);

#endif /* FLS_SIM_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
