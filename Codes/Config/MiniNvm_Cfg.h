/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniNvm_Cfg.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniNvm 可变配置参数头文件。本文件为示例配置。
 **
 **  @details            : 本文件定义 MiniNvm 的可变配置：
 **                        1) 块数量/长度上限/队列容量（块数量与长度上限跟随 MiniFee 配置）；
 **                        2) 块描述符类型（块号、MiniFee 块号、长度、RAM 地址）；
 **                        3) 每个块独立的 RAM buffer 声明（定义见 MiniNvm_Cfg.c）。
 **
 **  @note               本文件仅为**示例配置**，集成时请按实际工程替换块表与 RAM buffer。
 **                        描述符数量、顺序、Length 必须与 MiniFee_BlockConfig[] 严格一一对应，
 **                        否则 MiniNvm_Init 校验失败、模块不可用。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      重构：队列仅存元数据、新增 RAM 接口；配置精简为示例
 **
 ***********************************************************************************************************************/

#ifndef MININVM_CFG_H_
#define MININVM_CFG_H_

/* =================================================== inclusions =================================================== */
#include "Common.h"
#include "MiniFee_Cfg.h"

/* ===================================================== macros ===================================================== */
/**
 * @brief           逻辑块数量。
 * @note            跟随 MiniFee 配置（MINIFEE_BLOCK_MAX），不得单独修改。
 */
#define MININVM_BLOCK_COUNT             ((uint32)MINIFEE_BLOCK_MAX)

/**
 * @brief           单块最大长度（共享 DataBuf 按此定容）。
 * @note            跟随 MiniFee 配置（MINIFEE_MAX_BLOCK_DATA_SIZE）。
 */
#define MININVM_MAX_BLOCK_LENGTH        (MINIFEE_MAX_BLOCK_DATA_SIZE)

/**
 * @brief           环形请求队列容量。
 * @note            用户自定义参数，与块数、块长等其它参数没有必然关系：只需不小于上层同一时刻
 *                  可能提交且尚未处理的单块 ReadBlock/WriteBlock 请求数（ReadAll/WriteAll 不经队列）。
 *                  取值偏小则队满时入队返回 E_NOT_OK，取值偏大只是多占 RAM。
 */
#define MININVM_QUEUE_SIZE              (8u)

/* ================================================ type definitions ================================================ */
/**
 * @brief           块描述符：把一个 MiniNvm 逻辑块（BlockId 从 1 开始）映射到 MiniFee 块（从 0 开始）和 RAM buffer。
 */
typedef struct
{
    uint8 BlockId;                                  /**< MiniNvm 逻辑块号，从 1 开始，须等于数组下标+1 */
    MiniFee_BlockIdType MiniFeeBlockId;             /**< 对应 MiniFee 块号，从 0 开始，须等于数组下标 */
    uint32 Length;                                  /**< 逻辑数据长度，须等于 MiniFee_BlockConfig[].Length */
    uint8* RamBlockAddress;                         /**< 该块独立 RAM 缓冲区地址（不可为空） */
} MiniNvm_BlockDescriptorType;

/* ============================================ external data declarations =========================================== */
/**
 * @brief           每个 Block 的独立 RAM buffer（示例：长度等于对应块 Length）。
 */
extern uint8 MiniNvm_RamBlock_ExampleFlag[4u];
extern uint8 MiniNvm_RamBlock_ExampleConfig[10u];
extern uint8 MiniNvm_RamBlock_ExampleVssData[32u];

/**
 * @brief           块描述符表。
 * @note            数量、顺序、Length 必须与 MiniFee_BlockConfig[] 严格一一对应。
 */
extern const MiniNvm_BlockDescriptorType MiniNvm_BlockDescriptor[MININVM_BLOCK_COUNT];

#endif /* MININVM_CFG_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
