/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniFlsIf.c
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniFee 异步 Flash 平台适配实现（当前映射 RH850 Fls MCAL）。
 **
 **  @details            : 本文件是三个存储模块中唯一与平台绑定的文件，只把 Fls MCAL 的接口原样转发出去，
 **                        不修改地址、长度与数据内容，也不做同步等待。移植到其他 Flash 驱动时只改本文件即可。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      补充实现说明注释
 **
 ***********************************************************************************************************************/

/* =================================================== inclusions =================================================== */
#include "MiniFlsIf.h"
#include "SchM_Fls.h"
#include "Fls.h"

/* ========================================== external function definitions ========================================= */
/**
 * @brief           初始化底层 Flash 驱动：用 Fls 配置集完成初始化。
 * @return          uint8
 * @retval          E_OK: 始终返回成功。
 */
uint8 MiniFlsIf_Init(void)
{
    Fls_Init(FlsConfigSet);
    return E_OK;
}

/**
 * @brief           反初始化底层 Flash 驱动。
 * @return          uint8
 * @retval          E_OK: 始终返回成功。
 * @note            当前为空实现；若目标平台需关时钟/控制器，在此补充。
 */
uint8 MiniFlsIf_DeInit(void)
{
    return E_OK;
}

/**
 * @brief           发起异步读取：直接转发 Fls_Read。
 * @param[in]       offset: 相对 Flash 基址的偏移量。
 * @param[out]      buf: 目标缓冲，长度须不小于 len。
 * @param[in]       len: 读取字节数。
 * @return          uint8
 * @retval          E_OK: 请求已被底层接受（不代表完成）。
 * @retval          E_NOT_OK: 请求未被接受。
 */
uint8 MiniFlsIf_Read(uint32 offset, uint8* buf, uint32 len)
{
    return Fls_Read(offset, buf, len);
}

/**
 * @brief           发起异步写入：直接转发 Fls_Write。
 * @param[in]       offset: 相对 Flash 基址的偏移量。
 * @param[in]       buf: 源缓冲；底层异步持有其指针，调用后不可改写。
 * @param[in]       len: 写入字节数。
 * @return          uint8
 * @retval          E_OK: 请求已被底层接受（不代表完成）。
 * @retval          E_NOT_OK: 请求未被接受。
 */
uint8 MiniFlsIf_Write(uint32 offset, const uint8* buf, uint32 len)
{
    return Fls_Write(offset, buf, len);
}

/**
 * @brief           发起异步擦除：直接转发 Fls_Erase。
 * @param[in]       offset: 相对 Flash 基址的偏移量，须与擦除单元对齐。
 * @param[in]       len: 擦除长度，须为擦除单元整数倍。
 * @return          uint8
 * @retval          E_OK: 请求已被底层接受（不代表完成）。
 * @retval          E_NOT_OK: 请求未被接受。
 */
uint8 MiniFlsIf_Erase(uint32 offset, uint32 len)
{
    return Fls_Erase(offset, len);
}

/**
 * @brief           查询底层任务状态（含 MEMIF_BUSY / MEMIF_IDLE 等）。
 * @return          MemIf_StatusType
 */
MemIf_StatusType MiniFlsIf_GetStatus(void)
{
    return Fls_GetStatus();
}

/**
 * @brief           查询最近一次任务结果（MEMIF_JOB_OK / FAILED / PENDING / CANCELED 等）。
 * @return          MemIf_JobResultType
 */
MemIf_JobResultType MiniFlsIf_GetJobResult(void)
{
    return Fls_GetJobResult();
}

/**
 * @brief           周期推进底层异步任务：转发 Fls_MainFunction。
 * @return          void
 * @note            由系统调度器周期调用。
 */
void MiniFlsIf_MainFunction(void)
{
    Fls_MainFunction();
}

/*=======[E N D   O F   F I L E]===============================================*/
