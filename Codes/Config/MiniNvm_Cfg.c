/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm_Cfg.c
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: Manco
************************************************************************************************
*   描述         @: MiniNvm 可变配置数据定义：各块独立 RAM buffer 与块描述符表。
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
*   V1.1       2026/09/16    Manco              N/A         重构：队列仅存元数据、新增 RAM 接口；配置精简为示例
*
************************************************************************************************
* END_FILE_HDR*/
#include "MiniNvm_Cfg.h"

/**********************************************************************************************
* 配置数据定义（MiniNvm_Cfg.c）
* --------------------------------------------------------------------------------------------
* 本文件是 MiniNvm 唯一的可变数据定义处：
*   - MiniNvm_RamBlock_x：每个块独立的 RAM buffer（长度必须等于对应块 Length）；
*   - MiniNvm_BlockDescriptor[]：块描述符表，顺序与 MiniFee_BlockConfig[] 一一对应。
* 新增/删除块时，须同步维护这两处以及 MiniFee 的块配置。
* 以下均为示例数据，集成时按实际业务替换。
***********************************************************************************************/

/* 每个 Block 的独立 RAM buffer 定义（与 MiniFee 块配置一一对应） */
uint8 MiniNvm_RamBlock_ExampleFlag[4u];
uint8 MiniNvm_RamBlock_ExampleConfig[10u];
uint8 MiniNvm_RamBlock_ExampleVssData[32u];

/* 块描述符表（示例）：BlockId 1..N，MiniFeeBlockId 0..N-1，长度与 MiniFee 块配置一致。
 * 每行字段顺序：{ BlockId, MiniFeeBlockId, Length, RAM 地址 }。 */
const MiniNvm_BlockDescriptorType MiniNvm_BlockDescriptor[MININVM_BLOCK_COUNT] =
{
    { 1u, MINIFEE_BLOCK_EXAMPLE_FLAG,     4u,  MiniNvm_RamBlock_ExampleFlag },
    { 2u, MINIFEE_BLOCK_EXAMPLE_CONFIG,   10u, MiniNvm_RamBlock_ExampleConfig },
    { 3u, MINIFEE_BLOCK_EXAMPLE_VSS_DATA, 32u, MiniNvm_RamBlock_ExampleVssData }
};
