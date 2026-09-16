/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Fls.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境下的 Fls 驱动接口（模拟 MCAL Fls，异步 RAM 仿真）。
 **
 **  @details            : MiniFlsIf.c 是本工程唯一与平台绑定的文件，它把 MiniFee 的请求原样转发给本接口。
 **                        本头文件提供与 AUTOSAR Fls MCAL 一致的原型，使 MiniFlsIf.c、MiniFee 无需任何修改
 **                        即可在 Windows 上编译、运行与调试。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。目标平台使用真实 Fls MCAL。
 **
 ***********************************************************************************************************************/

#ifndef FLS_H_
#define FLS_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"
#include "MemIf_Types.h"

/* ================================================ type definitions ================================================ */
/**
 * @brief           Fls 配置参数（主机仿真使用；目标平台由 MCAL 生成）。
 */
typedef struct
{
    uint32 BaseAddress;   /**< 地址基址，须与 MINIFEE_FLS_BASE 一致 */
    uint32 TotalSize;     /**< 仿真 Flash 总大小（字节） */
    uint32 SectorSize;    /**< 擦除单元（扇区）大小，须与簇长度对齐 */
    uint32 PageSize;      /**< 最小写入单元（页），须与 MINIFEE_VIRTUALPAGE_SIZE 一致 */
} Fls_ConfigType;

/**
 * @brief           配置集（指针形式，MiniFlsIf.c 以 Fls_Init(FlsConfigSet) 形式调用）。
 */
extern const Fls_ConfigType* const FlsConfigSet;

/* ========================================== external function declarations ========================================= */
/**
 * @brief           初始化 Flash 驱动（异步语义：立即返回）。
 * @param[in]       ConfigPtr: 配置集。
 * @return          Std_ReturnType
 * @retval          E_OK: 初始化成功。
 * @retval          E_NOT_OK: 配置为空。
 */
extern Std_ReturnType Fls_Init(const Fls_ConfigType* ConfigPtr);

/**
 * @brief           发起异步读取：从 offset 起读 length 字节到 data。
 * @return          Std_ReturnType：E_OK 表示请求已接受。
 */
extern Std_ReturnType Fls_Read(uint32 offset, uint8* data, uint32 length);

/**
 * @brief           发起异步写入：把 data 起 length 字节写到 offset。
 * @return          Std_ReturnType：E_OK 表示请求已接受。
 * @note            仿真遵循“两次擦除之间只可编程一次”（仅允许 1 -> 0 位清除），越界或二次编程判为失败。
 */
extern Std_ReturnType Fls_Write(uint32 offset, const uint8* data, uint32 length);

/**
 * @brief           发起异步擦除：擦除 offset 起 length 字节（须按扇区对齐）。
 * @return          Std_ReturnType：E_OK 表示请求已接受。
 */
extern Std_ReturnType Fls_Erase(uint32 offset, uint32 length);

/**
 * @brief           查询驱动状态（MEMIF_BUSY / MEMIF_IDLE / MEMIF_UNINIT）。
 * @return          MemIf_StatusType
 */
extern MemIf_StatusType Fls_GetStatus(void);

/**
 * @brief           查询最近一次任务结果。
 * @return          MemIf_JobResultType
 */
extern MemIf_JobResultType Fls_GetJobResult(void);

/**
 * @brief           周期推进异步任务（每次推进至多完成一个作业）。
 * @return          void
 */
extern void Fls_MainFunction(void);

#endif /* FLS_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
