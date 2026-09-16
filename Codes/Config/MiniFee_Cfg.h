/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniFee_Cfg.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniFee 可变配置参数头文件（静态代码与配置参数隔离）。本文件为示例配置。
 **
 **  @details            : 本文件把 MiniFee 的“可变配置”与“静态代码”隔离，便于按产品/项目裁剪：
 **                        1) MINIFEE_FLS_BASE           ：Fls 地址基址（绝对地址 -> Fls 偏移 = 绝对地址 - 基址）；
 **                        2) MINIFEE_VIRTUALPAGE_SIZE   ：Flash 最小写入单元，用于数据物理对齐；
 **                        3) MINIFEE_MAX_BLOCK_DATA_SIZE：内部缓冲上限，须 >= 所有块 Length 的最大值；
 **                        4) MiniFee_BlockConfig[]      ：逻辑块表（块号 + 长度），顺序即逻辑顺序；
 **                        5) MiniFee_ClusterConfig[]    ：簇表（起始地址 + 长度），簇数须 >= 2 以支持轮换。
 **
 **  @note               本文件仅为**示例配置**，集成时请按实际工程替换地址基址、块表与簇表。
 **                       另外：MiniFee_BlockConfig[i].BlockNumber 必须等于 i；块数量由哨兵 MINIFEE_BLOCK_MAX 自动推导。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      重构：RAM 块目录、一次性读写；配置精简为示例
 **
 ***********************************************************************************************************************/

#ifndef MINIFEE_CFG_H_
#define MINIFEE_CFG_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"

/* ===================================================== macros ===================================================== */
/**
 * @brief           Fls 地址基址。
 * @note            内部所有 Fls 读写偏移 = 绝对地址 - MINIFEE_FLS_BASE，须与 Fls 驱动基址一致。
 */
#define MINIFEE_FLS_BASE                   (0xFF200000UL)

/**
 * @brief           Flash 最小写入单元。
 * @note            必须与 Fls 配置一致；MiniFee 用于内部物理长度对齐。
 */
#define MINIFEE_VIRTUALPAGE_SIZE           (4u)

/**
 * @brief           单块数据缓冲上限。
 * @note            内部缓冲区按此定容，须 >= 所有块 Length 的最大值（示例最大块为 32B）。
 */
#define MINIFEE_MAX_BLOCK_DATA_SIZE        (32u)

/* ================================================ type definitions ================================================ */
/**
 * @brief           逻辑块号枚举（示例）。
 * @note            每个块按其功能命名；BlockNumber 须等于数组下标（0..N-1），数组顺序即逻辑顺序。
 *                  新增块：在哨兵 MINIFEE_BLOCK_MAX 前增加枚举项，并在 MiniFee_BlockConfig[] 中增加对应条目，
 *                  MINIFEE_BLOCK_COUNT 由哨兵自动推导；MINIFEE_MAX_BLOCK_DATA_SIZE 须 >= 所有块 Length 最大值。
 */
typedef enum
{
    MINIFEE_BLOCK_EXAMPLE_FLAG = 0,       /**< 示例块：通用标志（4B） */
    MINIFEE_BLOCK_EXAMPLE_CONFIG,         /**< 示例块：配置参数（10B，演示非对齐长度） */
    MINIFEE_BLOCK_EXAMPLE_VSS_DATA,       /**< 示例块：批量数据（32B） */
    MINIFEE_BLOCK_MAX                     /**< 哨兵：块总数（= MINIFEE_BLOCK_COUNT），须置于最后 */
} MiniFee_BlockIdType;

/**
 * @brief           逻辑块配置项。
 */
typedef struct
{
    MiniFee_BlockIdType BlockNumber;   /**< 逻辑块号（枚举值，须等于数组下标 0..N-1） */
    uint32 Length;                     /**< 该块数据长度（字节，0 表示空块，不占用存储） */
} MiniFee_BlockConfigType;

/**
 * @brief           逻辑块总数。
 * @note            由枚举哨兵 MINIFEE_BLOCK_MAX 自动推导。
 */
#define MINIFEE_BLOCK_COUNT   ((uint32)MINIFEE_BLOCK_MAX)

/**
 * @brief           Cluster 配置项。
 */
typedef struct
{
    uint32 StartAddress;   /**< Cluster 起始地址（绝对地址） */
    uint32 Length;         /**< Cluster 长度（字节） */
} MiniFee_ClusterConfigType;

/**
 * @brief           Cluster 数量。
 * @note            须 >= 2 才能支持整簇轮换迁移；修改 MiniFee_ClusterConfig[] 条目时必须同步修改本宏。
 */
#define MINIFEE_CLUSTER_COUNT   (2u)

/* ============================================ external data declarations =========================================== */
/**
 * @brief           逻辑块配置表。
 * @note            每项 {BlockNumber, Length}；BlockNumber 须等于数组下标，Length 为该块数据长度（0 表示空块）。
 */
extern const MiniFee_BlockConfigType MiniFee_BlockConfig[MINIFEE_BLOCK_COUNT];

/**
 * @brief           Cluster 配置表。
 * @note            StartAddress 须与 Fls 擦除单元对齐，Length 须为 Fls 擦除单元整数倍，各簇不可重叠。
 */
extern const MiniFee_ClusterConfigType MiniFee_ClusterConfig[MINIFEE_CLUSTER_COUNT];

#endif /* MINIFEE_CFG_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
