/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniFee_Cfg.c
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: Manco
************************************************************************************************
*   描述         @: MiniFee 可变配置数据定义（块配置表 + Cluster 配置表）。
*                   本文件仅为**示例配置**，集成时请按实际工程替换。
*
************************************************************************************************
*   限制         @: 无
*
************************************************************************************************
*   修订历史:
*
*   版本       日期          编写人            CR#         描述
*   --------   -----------   ----------------   --------    -----------------------
*   V1.0       2026/09/10    Manco              N/A         初版发布
*   V1.1       2026/09/16    Manco              N/A         重构：RAM 块目录、一次性读写；配置精简为示例
*
************************************************************************************************
* END_FILE_HDR*/
#include "MiniFee_Cfg.h"

/* 块配置表（示例）：条目数由 MiniFee_Cfg.h 中 MINIFEE_BLOCK_COUNT 推导。
   BlockNumber 用 MiniFee_BlockIdType 枚举编号，须等于数组下标；Length 为各块数据长度（可不同）。
   Length 为 10 的块物理占用会向上对齐到 MINIFEE_VIRTUALPAGE_SIZE 的整数倍，用于演示对齐填充。 */
const MiniFee_BlockConfigType MiniFee_BlockConfig[MINIFEE_BLOCK_COUNT] =
{
    { MINIFEE_BLOCK_EXAMPLE_FLAG,     4u  },   /* 通用标志：4B */
    { MINIFEE_BLOCK_EXAMPLE_CONFIG,   10u },   /* 配置参数：10B（非 4 字节对齐） */
    { MINIFEE_BLOCK_EXAMPLE_VSS_DATA, 32u }    /* 批量数据：32B */
};

/* Cluster 配置表（示例）：条目数须与 MiniFee_Cfg.h 中 MINIFEE_CLUSTER_COUNT 一致。
   StartAddress 须与 Fls 擦除单元对齐，Length 须为擦除单元整数倍，各簇不可重叠。 */
const MiniFee_ClusterConfigType MiniFee_ClusterConfig[MINIFEE_CLUSTER_COUNT] =
{
    { 0xFF200000UL, 0x2000UL },   /* Cluster 0 */
    { 0xFF202000UL, 0x2000UL }    /* Cluster 1 */
};
