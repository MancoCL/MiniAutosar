/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniFee.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniFee EEPROM 仿真对外接口（基于 Flash 驱动的非易失存储抽象层）。
 **
 **  @details            : 按块号整块读写：块号由 MiniFee_BlockIdType 枚举定义，块大小由 MiniFee_BlockConfig[]
 **                        逐块配置。采用追加式写入 + 新记录遮蔽旧记录 + 整簇迁移回收；无 Valid/Invalid 标志页，
 **                        任何区域仅一次编程。
 **
 **  @note               : 完全异步：MiniFee_Read / MiniFee_Write 立即返回，调用方须周期调用
 **                        MiniFee_MainFunction 推进状态机，并用 MiniFee_GetStatus 查询结果。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      重构：RAM 块目录、一次性读写接口
 **
 ***********************************************************************************************************************/

#ifndef MINIFEE_H_
#define MINIFEE_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"
#include "MemM.h"
#include "MiniFee_Cfg.h"   /* 可变配置参数（块配置表、Cluster 配置表、地址基址、块枚举等） */

/* ===================================================== macros ===================================================== */
/**
 * @brief           Cluster 头大小（字节）。
 * @note            Magic(4) + Generation(3) + CheckSum(1)；由存储格式决定，不可配置。
 */
#define MINIFEE_CLUSTER_HEADER_SIZE        (8u)

/**
 * @brief           块头大小（字节）。
 * @note            BlockNumber(3) + Length(4) + CheckSum(1)，无标志页；由存储格式决定，不可配置。
 */
#define MINIFEE_BLOCK_HEADER_SIZE          (8u)

/**
 * @brief           数据长度按 Flash 最小写入单元向上对齐。
 * @param[in]       len: 逻辑数据长度。
 * @return          对齐后的物理长度。
 */
#define MINIFEE_ALIGN_LEN(len)             (((len) + (MINIFEE_VIRTUALPAGE_SIZE - 1u)) & ~(MINIFEE_VIRTUALPAGE_SIZE - 1u))

/**
 * @brief           一条记录的物理总大小（块头 + 数据，变长）。
 * @param[in]       len: 已对齐的数据长度。
 * @return          块头 + 数据的总字节数。
 */
#define MINIFEE_BLOCK_TOTAL_OF(len)        (MINIFEE_BLOCK_HEADER_SIZE + (len))

/**
 * @brief           Flash 擦除后的字节值，用于判定空白。
 */
#define MINIFEE_ERASED_VALUE               (0xFFu)

/**
 * @brief           Cluster 头 Magic（4 字节，大端），用于识别已初始化区域；无 Status 字段。
 */
#define MINIFEE_CLUSTER_MAGIC              (0xAA55AA44UL)

/* ================================================ type definitions ================================================ */
/**
 * @brief           块头（序列化为 MINIFEE_BLOCK_HEADER_SIZE 字节，大端）。
 * @note            字段：BlockNumber = 块号（3B，等于配置数组下标）；Length = 该块逻辑数据长度（4B）；
 *                  CheckSum = CRC-8（1B，多项式 0x07，输入为 BlockNumber(3) + Length(4) 共 7 字节）。
 *                  数据固定紧随块头（偏移 +MINIFEE_BLOCK_HEADER_SIZE）；“上一条记录”由 RAM 块目录维护，不再存于块头。
 */
typedef struct
{
    uint32 BlockNumber;    /**< 逻辑块号（= 配置数组下标，3 字节大端存储） */
    uint32 Length;         /**< 该块逻辑数据长度（4 字节大端存储） */
    uint8  CheckSum;       /**< CRC-8（多项式 0x07） */
} MiniFee_BlockHeaderType;

/**
 * @brief           异步任务状态。
 */
typedef enum
{
    MINIFEE_STATUS_IDLE = 0,   /**< 空闲，可接受新请求 */
    MINIFEE_STATUS_BUSY,       /**< 任务进行中 */
    MINIFEE_STATUS_OK,         /**< 最近一次任务成功 */
    MINIFEE_STATUS_NOT_OK      /**< 最近一次任务失败/被取消 */
} MiniFee_StatusType;

/**
 * @brief           一次性读回调：MiniFee 按块目录逐块读取每块最新记录并回调交付。
 * @param[in]       blockNumber: MiniFee 块号（0 起）。
 * @param[in]       data: 记录数据指针。
 * @param[in]       length: 逻辑长度。
 * @return          void
 * @note            无记录的块不回调；回调在 MiniFee_MainFunction 上下文中同步执行，调用前须保证回调内目标缓冲有效。
 */
typedef void (*MiniFee_ReadAllCallbackType)(uint8 blockNumber, const uint8* data, uint32 length);

/**
 * @brief           一次性写数据回调：返回指定块待写入数据的指针。
 * @param[in]       blockNumber: MiniFee 块号（0 起）。
 * @return          该块待写数据指针；返回 NULL_PTR 表示该块无需写入（跳过）。
 * @note            MiniFee_WriteAll 单遍选簇后按块号 0..N-1 逐个回调取数并追加记录，数据长度取自
 *                  MiniFee_BlockConfig[].Length；回调会被“预统计占用”与“实际写入/迁移”多次调用，须稳定
 *                  （同一块多次调用返回一致，且数据在整个写作业期间有效）。
 */
typedef const uint8* (*MiniFee_WriteAllDataCallbackType)(uint8 blockNumber);

/* ========================================== external function declarations ========================================= */
/**
 * @brief           初始化 MiniFlsIf 并复位异步上下文。
 * @return          void
 * @note            异步模式：须周期调用 MiniFee_MainFunction 推进状态机，并用 MiniFee_GetStatus 查询结果；
 *                  Fls 推进与喂狗由调用方在外部循环负责。
 */
extern void MiniFee_Init(void);

/**
 * @brief           发起指定 Block 的异步整块读。
 * @param[in]       blockNumber: MiniFee_BlockIdType 枚举值。
 * @param[in]       size: 逻辑长度，必须等于 MiniFee_BlockConfig[blockNumber].Length。
 * @param[out]      buf: 目标缓冲。
 * @return          uint8
 * @retval          E_OK: 请求被接受（不代表完成）。
 * @retval          E_NOT_OK: 参数非法或模块忙。
 */
extern uint8 MiniFee_Read(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf);

/**
 * @brief           发起指定 Block 的异步整块写。
 * @param[in]       blockNumber: MiniFee_BlockIdType 枚举值。
 * @param[in]       size: 逻辑长度，必须等于 MiniFee_BlockConfig[blockNumber].Length。
 * @param[in]       buf: 源缓冲（作业期间须保持有效）。
 * @return          uint8
 * @retval          E_OK: 请求被接受（不代表完成）。
 * @retval          E_NOT_OK: 参数非法或模块忙。
 */
extern uint8 MiniFee_Write(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf);

/**
 * @brief           发起异步一次性读全部块（单遍扫描活动簇，逐条记录回调交付）。
 * @param[in]       callback: 交付回调。
 * @return          uint8
 * @retval          E_OK: 请求被接受（不代表完成）。
 * @retval          E_NOT_OK: 回调为空或模块忙。
 */
extern uint8 MiniFee_ReadAll(MiniFee_ReadAllCallbackType callback);

/**
 * @brief           发起异步一次性写全部块（预统计占用、单遍选簇后按块号连续追加）。
 * @param[in]       callback: 取数回调。
 * @return          uint8
 * @retval          E_OK: 请求被接受（不代表完成）。
 * @retval          E_NOT_OK: 回调为空或模块忙。
 */
extern uint8 MiniFee_WriteAll(MiniFee_WriteAllDataCallbackType callback);

/**
 * @brief           取消当前异步任务。
 * @return          uint8
 * @retval          E_OK: 已取消，结果置 NOT_OK。
 * @retval          E_NOT_OK: 当前非 BUSY。
 * @note            不撤销已在底层进行的 Flash 操作，仅让上层尽快看到失败结果。
 */
extern uint8 MiniFee_Cancel(void);

/**
 * @brief           推进状态机（每次只推进有限状态，不同步等待 Flash）。
 * @return          void
 */
extern void MiniFee_MainFunction(void);

/**
 * @brief           查询任务状态。
 * @return          MiniFee_StatusType
 */
extern MiniFee_StatusType MiniFee_GetStatus(void);

#endif /* MINIFEE_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
