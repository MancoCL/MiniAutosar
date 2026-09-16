/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniFlsIf.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniFee 使用的异步 Flash 平台适配接口，屏蔽底层具体 Flash 驱动。
 **
 **  @details            : 本模块是 MiniFee 与底层 Flash 驱动（当前为 RH850 Fls MCAL）之间的“薄适配层”：
 **                        1) 只做转发，不实现任何 Flash 算法（块管理、数据格式、校验、迁移、磨损均衡均由 MiniFee 负责）；
 **                        2) 全部接口保持底层异步语义：读/写/擦除返回 E_OK 仅代表“请求已被接受”，不代表操作完成；
 **                        3) 完成状态须经 MiniFlsIf_GetStatus（是否仍 MEMIF_BUSY）与 MiniFlsIf_GetJobResult 查询；
 **                        4) 移植到其他平台时只需修改 MiniFlsIf.c，本头文件与 MiniFee / MiniNvm 均无需改动。
 **
 **  @note               : 周期调度顺序必须为 MiniFlsIf_MainFunction -> MiniFee_MainFunction -> MiniNvm_MainFunction，
 **                        否则各层状态无法收敛。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      同步接口说明
 **
 ***********************************************************************************************************************/

#ifndef MINIFLSIF_H_
#define MINIFLSIF_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"
#include "MemIf_Types.h"

/* ========================================== external function declarations ========================================= */
/**
 * @brief           初始化底层 Flash 驱动。
 * @return          uint8
 * @retval          E_OK: 始终返回成功。
 * @note            当前实现调用 Fls_Init(FlsConfigSet)。
 */
extern uint8 MiniFlsIf_Init(void);

/**
 * @brief           反初始化底层 Flash 驱动。
 * @return          uint8
 * @retval          E_OK: 始终返回成功。
 * @note            当前为空实现；若目标平台需关闭 Flash 控制器或时钟，应在此补充。
 */
extern uint8 MiniFlsIf_DeInit(void);

/**
 * @brief           发起异步读取：把从 offset 起的 len 字节读到 buf。
 * @param[in]       offset: 相对 Flash 基址的偏移量。
 * @param[out]      buf: 目标缓冲，长度须不小于 len。
 * @param[in]       len: 读取字节数。
 * @return          uint8
 * @retval          E_OK: 请求已被底层接受，不代表读取已完成。
 * @retval          E_NOT_OK: 请求未被接受。
 */
extern uint8 MiniFlsIf_Read(uint32 offset, uint8* buf, uint32 len);

/**
 * @brief           发起异步写入：把 src 中 len 字节写到 offset 起的位置。
 * @param[in]       offset: 相对 Flash 基址的偏移量。
 * @param[in]       buf: 源缓冲；底层异步持有其指针，调用返回后不可改写。
 * @param[in]       len: 写入字节数。
 * @return          uint8
 * @retval          E_OK: 请求已被底层接受，不代表写入已完成。
 * @retval          E_NOT_OK: 请求未被接受。
 */
extern uint8 MiniFlsIf_Write(uint32 offset, const uint8* buf, uint32 len);

/**
 * @brief           发起异步擦除：擦除从 offset 起长度为 len 的区域。
 * @param[in]       offset: 相对 Flash 基址的偏移量，须与擦除单元对齐。
 * @param[in]       len: 擦除长度，须为擦除单元整数倍。
 * @return          uint8
 * @retval          E_OK: 请求已被底层接受，不代表擦除已完成。
 * @retval          E_NOT_OK: 请求未被接受。
 */
extern uint8 MiniFlsIf_Erase(uint32 offset, uint32 len);

/**
 * @brief           查询底层任务状态。
 * @return          MemIf_StatusType
 * @retval          MEMIF_UNINIT: 未初始化。
 * @retval          MEMIF_IDLE: 空闲。
 * @retval          MEMIF_BUSY: 忙。
 * @retval          MEMIF_BUSY_INTERNAL: 内部忙。
 */
extern MemIf_StatusType MiniFlsIf_GetStatus(void);

/**
 * @brief           查询最近一次任务结果。
 * @return          MemIf_JobResultType
 * @retval          MEMIF_JOB_OK: 成功。
 * @retval          MEMIF_JOB_FAILED: 失败。
 * @retval          MEMIF_JOB_PENDING: 进行中。
 * @retval          MEMIF_JOB_CANCELED: 已取消。
 * @note            仅当 MiniFlsIf_GetStatus 返回 MEMIF_IDLE 时结果才有意义。
 */
extern MemIf_JobResultType MiniFlsIf_GetJobResult(void);

/**
 * @brief           周期推进底层 Flash 异步任务（转发 Fls_MainFunction）。
 * @return          void
 * @note            须由系统调度器周期调用。
 */
extern void MiniFlsIf_MainFunction(void);

#endif /* MINIFLSIF_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
