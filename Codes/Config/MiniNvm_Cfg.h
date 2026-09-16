/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm_Cfg.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: Manco
************************************************************************************************
*   描述         @: MiniNvm 可变配置参数头文件：块数量/队列容量、块描述符类型与各块 RAM buffer 声明。
*                   本文件仅为**示例配置**，集成时请按实际工程替换块表与 RAM buffer。
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

#ifndef MININVM_CFG_H_
#define MININVM_CFG_H_

#include "Common.h"
#include "MiniFee_Cfg.h"

/* 块数量与长度上限跟随 MiniFee 配置（MINIFEE_BLOCK_MAX / MINIFEE_MAX_BLOCK_DATA_SIZE） */
#define MININVM_BLOCK_COUNT             ((uint32)MINIFEE_BLOCK_MAX)
#define MININVM_MAX_BLOCK_LENGTH        (MINIFEE_MAX_BLOCK_DATA_SIZE)

/* 单块请求队列容量（用户自定义，示例值 8）：
 * 只约束同一时刻排队等待处理的单块 ReadBlock/WriteBlock 请求数，与块数、块长无关；
 * ReadAll/WriteAll 为一次性作业，不经队列，规模再大也不占用队列。 */
#define MININVM_QUEUE_SIZE              (8u)

/* 块描述符：把 MiniNvm 逻辑块映射到 MiniFee 块与上层 RAM buffer */
typedef struct
{
    uint8 BlockId;                       /* MiniNvm 逻辑块号（从 1 开始），须等于数组下标 + 1 */
    MiniFee_BlockIdType MiniFeeBlockId;  /* 对应 MiniFee 块号（从 0 开始），须等于数组下标 */
    uint32 Length;                       /* 逻辑数据长度，须等于 MiniFee_BlockConfig[].Length */
    uint8* RamBlockAddress;              /* 该块独立 RAM buffer 地址，不可为空 */
} MiniNvm_BlockDescriptorType;

/* 每个 Block 的独立 RAM buffer（示例：长度等于对应块 Length） */
extern uint8 MiniNvm_RamBlock_ExampleFlag[4u];
extern uint8 MiniNvm_RamBlock_ExampleConfig[10u];
extern uint8 MiniNvm_RamBlock_ExampleVssData[32u];

extern const MiniNvm_BlockDescriptorType MiniNvm_BlockDescriptor[MININVM_BLOCK_COUNT];

#endif /* MININVM_CFG_H_ */
