/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniFee_Cfg.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniFee 可变配置参数头文件（静态代码与配置参数隔离），
*                   包含地址基址、Flash 最小写单元、块配置表与 Cluster 配置表。
*
************************************************************************************************
*   限制         @: 无
*
************************************************************************************************
*   修订历史:
*
*   版本       日期          编写人            CR#         描述
*   --------   -----------   ----------------   --------    -----------------------
*   V1.0       2026/09/10    CaoLiang           N/A         初版发布
*
************************************************************************************************
* END_FILE_HDR*/

#ifndef MINIFEE_CFG_H_
#define MINIFEE_CFG_H_
#include "Common.h"

/*********************************************************************************************************************/
/* 可变配置宏                                                                                                         */
/*********************************************************************************************************************/
/* Fls 地址基址：内部所有 Fls 读写偏移 = 绝对地址 - MINIFEE_FLS_BASE，须与 Fls 驱动基址一致 */
#define MINIFEE_FLS_BASE                   (0xFF200000UL)

/* Flash 最小写入单元，必须与 Fls 配置一致；MiniFee 用于内部物理长度对齐 */
#define MINIFEE_VIRTUALPAGE_SIZE           (4u)

/* 单块数据缓冲上限：内部缓冲区按此定容，须 ≥ 所有块 Length 的最大值（当前最大为 VSS 数据 880B） */
#define MINIFEE_MAX_BLOCK_DATA_SIZE        (880u)

/*********************************************************************************************************************/
/* 块配置表：类型 + 数量 + 外部声明（定义见 MiniFee_Cfg.c）                                                            */
/* 每个块按其功能命名，Length 为该块数据长度（字节，0 表示空块）；                                                      */
/* BlockNumber 须等于数组下标（0..N-1），数组顺序即逻辑顺序；                                                           */
/* 新增块：在哨兵 MINIFEE_BLOCK_MAX 前增加枚举项，并在 MiniFee_BlockConfig[] 中增加对应条目，                          */
/* MINIFEE_BLOCK_COUNT 由哨兵自动推导。MINIFEE_MAX_BLOCK_DATA_SIZE 须 ≥ 所有块 Length 最大值。                        */
/*********************************************************************************************************************/
typedef enum
{
    MINIFEE_BLOCK_APP_BLOCK_ID = 0,       /* 应用块标识（AppA/AppB/标定） */
    MINIFEE_BLOCK_REPROGRAM_FLAG,         /* 重编程标志 */
    MINIFEE_BLOCK_RESET_FLAG,             /* 复位会话标志 */
    MINIFEE_BLOCK_SECURITY_FLAG,          /* 安全访问标志 */
    MINIFEE_BLOCK_NEGATIVE_RESP_FLAG,     /* 负响应标志 */
    MINIFEE_BLOCK_ALLOW_F187_WRITE,       /* F187 写入允许标志 */
    MINIFEE_BLOCK_ALLOW_F190_WRITE,       /* F190 写入允许标志 */
    MINIFEE_BLOCK_PROGRAM_COUNT,          /* 程序刷写计数 */
    MINIFEE_BLOCK_DID_F100,               /* DID F100 */
    MINIFEE_BLOCK_DID_F110,               /* DID F110 */
    MINIFEE_BLOCK_DID_F111,               /* DID F111 */
    MINIFEE_BLOCK_DID_F112,               /* DID F112 */
    MINIFEE_BLOCK_DID_F113,               /* DID F113 */
    MINIFEE_BLOCK_DID_F114,               /* DID F114 */
    MINIFEE_BLOCK_DID_F115,               /* DID F115 */
    MINIFEE_BLOCK_DID_F116,               /* DID F116 */
    MINIFEE_BLOCK_DID_F117,               /* DID F117 */
    MINIFEE_BLOCK_DID_F118,               /* DID F118 */
    MINIFEE_BLOCK_DID_F119,               /* DID F119 */
    MINIFEE_BLOCK_DID_F11A,               /* DID F11A */
    MINIFEE_BLOCK_DID_F11B,               /* DID F11B */
    MINIFEE_BLOCK_DID_F11C,               /* DID F11C */
    MINIFEE_BLOCK_DID_F11D,               /* DID F11D */
    MINIFEE_BLOCK_DID_F11E,               /* DID F11E */
    MINIFEE_BLOCK_DID_F11F,               /* DID F11F */
    MINIFEE_BLOCK_DID_F120,               /* DID F120 */
    MINIFEE_BLOCK_DID_F121,               /* DID F121 */
    MINIFEE_BLOCK_DID_F183,               /* DID F183 */
    MINIFEE_BLOCK_DID_F187,               /* DID F187 */
    MINIFEE_BLOCK_DID_F18A,               /* DID F18A */
    MINIFEE_BLOCK_DID_F18B,               /* DID F18B */
    MINIFEE_BLOCK_DID_F18C,               /* DID F18C */
    MINIFEE_BLOCK_DID_F190,               /* DID F190 */
    MINIFEE_BLOCK_DID_F191,               /* DID F191 */
    MINIFEE_BLOCK_DID_F192,               /* DID F192 */
    MINIFEE_BLOCK_DID_F194,               /* DID F194 */
    MINIFEE_BLOCK_DID_F198,               /* DID F198 */
    MINIFEE_BLOCK_DID_F1A0,               /* DID F1A0 */
    MINIFEE_BLOCK_DID_F1A1,               /* DID F1A1 */
    MINIFEE_BLOCK_DID_F1A2,               /* DID F1A2 */
    MINIFEE_BLOCK_DID_F1A5,               /* DID F1A5 */
    MINIFEE_BLOCK_DID_F1A8,               /* DID F1A8 */
    MINIFEE_BLOCK_DID_F1A9,               /* DID F1A9 */
    MINIFEE_BLOCK_DID_F1AA,               /* DID F1AA */
    MINIFEE_BLOCK_DID_F130,               /* DID F130 */
    MINIFEE_BLOCK_DID_F1B5,               /* DID F1B5 */
    MINIFEE_BLOCK_DID_F1B6,               /* DID F1B6 */
    MINIFEE_BLOCK_DID_AFF1,               /* DID AFF1 */
    MINIFEE_BLOCK_DID_AFF2,               /* DID AFF2 */
    MINIFEE_BLOCK_DID_AFF5,               /* DID AFF5 */
    MINIFEE_BLOCK_DID_AFFC,               /* DID AFFC */
    MINIFEE_BLOCK_DID_AFFD,               /* DID AFFD */
    MINIFEE_BLOCK_DID_AFFE,               /* DID AFFE */
    MINIFEE_BLOCK_DID_AFFF,               /* DID AFFF */
    MINIFEE_BLOCK_DID_A333,               /* DID A333 */
    MINIFEE_BLOCK_DID_A444,               /* DID A444 */
    MINIFEE_BLOCK_DID_A555,               /* DID A555 */
    MINIFEE_BLOCK_VSS_DATA,               /* 车速信号数据 */
    MINIFEE_BLOCK_MAX                     /* 哨兵：块总数（= MINIFEE_BLOCK_COUNT），须置于最后 */
} MiniFee_BlockIdType;

typedef struct
{
    MiniFee_BlockIdType BlockNumber;   /* 逻辑块号（枚举值，须等于数组下标 0..N-1） */
    uint32 Length;                     /* 该块数据长度（字节，0 表示空块，不占用存储） */
} MiniFee_BlockConfigType;

#define MINIFEE_BLOCK_COUNT   ((uint32)MINIFEE_BLOCK_MAX)

extern const MiniFee_BlockConfigType MiniFee_BlockConfig[MINIFEE_BLOCK_COUNT];

/*********************************************************************************************************************/
/* Cluster 配置表：类型 + 数量 + 外部声明（定义见 MiniFee_Cfg.c）                                                     */
/* StartAddress 须与 Fls 擦除单元对齐，Length 须为 Fls 擦除单元整数倍，Cluster 数量须≥2；                              */
/* 修改配置表条目时，必须同步更新 MINIFEE_CLUSTER_COUNT。                                                             */
/*********************************************************************************************************************/
typedef struct
{
    uint32 StartAddress;   /* Cluster 起始地址（绝对地址） */
    uint32 Length;         /* Cluster 长度（字节） */
} MiniFee_ClusterConfigType;

#define MINIFEE_CLUSTER_COUNT   (2u)

extern const MiniFee_ClusterConfigType MiniFee_ClusterConfig[MINIFEE_CLUSTER_COUNT];

#endif /* MINIFEE_CFG_H_ */