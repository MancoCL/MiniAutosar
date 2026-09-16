/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniNvm.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniNvm 对外接口：管理 RAM Block、排队单块/多块异步请求并维护请求结果。
 **
 **  @details            : 实际存储由 MiniFee 完成。API 返回 E_OK 仅表示请求已接受，最终结果经
 **                        MiniNvm_GetErrorStatus / MiniNvm_GetMultiJobStatus 查询。
 **
 **  @note               : MiniNvm 是精简版 NvM：维护每个块的 RAM buffer、一个环形异步请求队列和每块的最近结果。
 **                        典型用法：
 **                        1) 初始化：MiniNvm_Init()（内部初始化 MiniFee 并校验配置）；
 **                        2) 运行期：用 GetRamBuffer / ReadRam / WriteRam 直接读写 RAM buffer（不触发 Flash），
 **                           WriteRam 在数据实际变化时置脏标记；
 **                        3) 下电/复位/跳转前：MiniNvm_WriteAll() 把脏块写回非易失存储；
 **                        4) 周期调度：必须按 MiniFlsIf_MainFunction -> MiniFee_MainFunction -> MiniNvm_MainFunction
 **                           的顺序调用，MiniNvm 自身不推进 MiniFee。
 **                        编号约定：MiniNvm 的 BlockId 从 1 开始；MiniFee 的块号从 0 开始，二者由描述符一一映射。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      重构：一次性读写、RAM 同步接口，移除默认值恢复
 **
 ***********************************************************************************************************************/

#ifndef MININVM_H_
#define MININVM_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"
#include "MiniFee.h"
#include "MiniNvm_Cfg.h"

/* ================================================ type definitions ================================================ */
/**
 * @brief           请求结果：单块结果与多块总体结果共用此类型。
 */
typedef enum
{
    MININVM_REQ_IDLE = 0u,            /**< 无请求/空闲 */
    MININVM_REQ_PENDING,              /**< 请求进行中 */
    MININVM_REQ_OK,                   /**< 成功 */
    MININVM_REQ_NOT_OK                /**< 失败或被取消 */
} MiniNvm_RequestResultType;

/* ========================================== external function declarations ========================================= */
/**
 * @brief           初始化 MiniFee、校验块描述符、复位队列与结果。
 * @return          void
 * @note            校验失败时模块不可用，后续所有请求返回 E_NOT_OK。
 */
void MiniNvm_Init(void);

/**
 * @brief           反初始化：复位为未初始化状态，清空队列与结果。
 * @return          void
 */
void MiniNvm_DeInit(void);

/**
 * @brief           请求异步读取指定 Block 到 dstPtr。
 * @param[in]       blockId: MiniNvm 块号（从 1 开始）。
 * @param[out]      dstPtr: 目标缓冲。
 * @return          uint8
 * @retval          E_OK: 已入队。
 * @retval          E_NOT_OK: 参数非法、模块未初始化或模块忙。
 */
uint8 MiniNvm_ReadBlock(uint8 blockId, uint8* dstPtr);

/**
 * @brief           请求异步写入指定 Block。
 * @param[in]       blockId: MiniNvm 块号（从 1 开始）。
 * @param[in]       srcPtr: 源数据；入队时即快照，入队返回后可复用。
 * @return          uint8
 * @retval          E_OK: 已入队；若与当前 RAM buffer 一致则直接返回（内容去重，不入队）。
 * @retval          E_NOT_OK: 参数非法、模块未初始化或模块忙。
 */
uint8 MiniNvm_WriteBlock(uint8 blockId, const uint8* srcPtr);

/**
 * @brief           一次性读取全部 Block 到各自 RAM buffer（MiniFee 单遍扫描活动簇）。
 * @return          uint8
 * @retval          E_OK: 请求被接受。
 * @retval          E_NOT_OK: 模块未初始化或已有作业进行中。
 */
uint8 MiniNvm_ReadAll(void);

/**
 * @brief           一次性写入全部脏 Block（MiniFee 单遍选簇后连续追加）。
 * @return          uint8
 * @retval          E_OK: 请求被接受（无脏块时不写入）。
 * @retval          E_NOT_OK: 模块未初始化或已有作业进行中。
 */
uint8 MiniNvm_WriteAll(void);

/**
 * @brief           取消当前作业与队列中的所有请求，相关结果置 NOT_OK。
 * @return          uint8
 * @retval          E_OK: 已取消。
 * @retval          E_NOT_OK: 无作业可取消。
 */
uint8 MiniNvm_CancelJobs(void);

/**
 * @brief           查询指定单块最近一次请求结果。
 * @param[in]       blockId: MiniNvm 块号（从 1 开始）。
 * @param[out]      resultPtr: 结果输出。
 * @return          uint8
 * @retval          E_OK: 查询成功。
 * @retval          E_NOT_OK: 参数非法。
 */
uint8 MiniNvm_GetErrorStatus(uint8 blockId, MiniNvm_RequestResultType* resultPtr);

/**
 * @brief           查询多块作业（ReadAll/WriteAll）总体结果。
 * @return          MiniNvm_RequestResultType
 * @note            所有子请求出清后才变为 OK/NOT_OK，期间保持 PENDING。
 */
MiniNvm_RequestResultType MiniNvm_GetMultiJobStatus(void);

/**
 * @brief           周期推进：从队列取请求、启动或检查 MiniFee 作业、更新并收尾结果。
 * @return          void
 */
void MiniNvm_MainFunction(void);

/**
 * @brief           获取指定块的 RAM buffer 地址（同步，不触发 Flash 作业）。
 * @param[in]       blockNumber: MiniFee_BlockIdType（从 0 开始）。
 * @return          uint8*: RAM buffer 地址；参数非法时返回 NULL_PTR。
 */
uint8* MiniNvm_GetRamBuffer(MiniFee_BlockIdType blockNumber);

/**
 * @brief           从指定块的 RAM buffer 同步读取（不触发 Flash 作业）。
 * @param[in]       blockNumber: MiniFee_BlockIdType（从 0 开始）。
 * @param[out]      dstPtr: 目标缓冲。
 * @param[in]       length: 读取长度（不得超过该块长度）。
 * @return          uint8
 * @retval          E_OK: 读取成功。
 * @retval          E_NOT_OK: 参数非法。
 */
uint8 MiniNvm_ReadRam(MiniFee_BlockIdType blockNumber, uint8* dstPtr, uint32 length);

/**
 * @brief           同步写入指定块的 RAM buffer，并在数据实际变化时将该块标记为脏。
 * @param[in]       blockNumber: MiniFee_BlockIdType（从 0 开始）。
 * @param[in]       srcPtr: 源数据。
 * @param[in]       length: 写入长度（不得超过该块长度）。
 * @return          uint8
 * @retval          E_OK: 写入成功（含“与原值一致、无需改动”的情形）。
 * @retval          E_NOT_OK: 参数非法。
 * @note            运行期修改必须经本接口才会置脏，才能被 MiniNvm_WriteAll 落盘。
 */
uint8 MiniNvm_WriteRam(MiniFee_BlockIdType blockNumber, const uint8* srcPtr, uint32 length);

#endif /* MININVM_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
