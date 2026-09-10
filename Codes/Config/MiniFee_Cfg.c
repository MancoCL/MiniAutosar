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
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniFee 可变配置数据定义（块配置表 + Cluster 配置表）
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
#include "MiniFee_Cfg.h"

/* 块配置表（条目数由 MiniFee_Cfg.h 中 MINIFEE_BLOCK_COUNT 推导；
   BlockNumber 用 MiniFee_BlockIdType 枚举编号，须等于数组下标，Length 为各块数据长度，可不同） */
const MiniFee_BlockConfigType MiniFee_BlockConfig[MINIFEE_BLOCK_COUNT] =
{
    { MINIFEE_BLOCK_APP_BLOCK_ID,       2u   },   /* 应用块标识：2B */
    { MINIFEE_BLOCK_REPROGRAM_FLAG,     4u   },   /* 重编程标志：4B */
    { MINIFEE_BLOCK_RESET_FLAG,         4u   },   /* 复位会话标志：4B */
    { MINIFEE_BLOCK_SECURITY_FLAG,      4u   },   /* 安全访问标志：4B */
    { MINIFEE_BLOCK_NEGATIVE_RESP_FLAG, 4u   },   /* 负响应标志：4B */
    { MINIFEE_BLOCK_ALLOW_F187_WRITE,   1u   },   /* F187 写入允许标志：1B */
    { MINIFEE_BLOCK_ALLOW_F190_WRITE,   1u   },   /* F190 写入允许标志：1B */
    { MINIFEE_BLOCK_PROGRAM_COUNT,      2u   },   /* 程序刷写计数：2B */
    { MINIFEE_BLOCK_DID_F100,           6u   },   /* DID F100 */
    { MINIFEE_BLOCK_DID_F110,           16u  },   /* DID F110 */
    { MINIFEE_BLOCK_DID_F111,           16u  },   /* DID F111 */
    { MINIFEE_BLOCK_DID_F112,           16u  },   /* DID F112 */
    { MINIFEE_BLOCK_DID_F113,           16u  },   /* DID F113 */
    { MINIFEE_BLOCK_DID_F114,           16u  },   /* DID F114 */
    { MINIFEE_BLOCK_DID_F115,           16u  },   /* DID F115 */
    { MINIFEE_BLOCK_DID_F116,           16u  },   /* DID F116 */
    { MINIFEE_BLOCK_DID_F117,           16u  },   /* DID F117 */
    { MINIFEE_BLOCK_DID_F118,           16u  },   /* DID F118 */
    { MINIFEE_BLOCK_DID_F119,           16u  },   /* DID F119 */
    { MINIFEE_BLOCK_DID_F11A,           16u  },   /* DID F11A */
    { MINIFEE_BLOCK_DID_F11B,           16u  },   /* DID F11B */
    { MINIFEE_BLOCK_DID_F11C,           16u  },   /* DID F11C */
    { MINIFEE_BLOCK_DID_F11D,           16u  },   /* DID F11D */
    { MINIFEE_BLOCK_DID_F11E,           16u  },   /* DID F11E */
    { MINIFEE_BLOCK_DID_F11F,           16u  },   /* DID F11F */
    { MINIFEE_BLOCK_DID_F120,           16u  },   /* DID F120 */
    { MINIFEE_BLOCK_DID_F121,           16u  },   /* DID F121 */
    { MINIFEE_BLOCK_DID_F183,           10u  },   /* DID F183 */
    { MINIFEE_BLOCK_DID_F187,           5u   },   /* DID F187 */
    { MINIFEE_BLOCK_DID_F18A,           5u   },   /* DID F18A */
    { MINIFEE_BLOCK_DID_F18B,           3u   },   /* DID F18B */
    { MINIFEE_BLOCK_DID_F18C,           16u  },   /* DID F18C */
    { MINIFEE_BLOCK_DID_F190,           17u  },   /* DID F190 */
    { MINIFEE_BLOCK_DID_F191,           5u   },   /* DID F191 */
    { MINIFEE_BLOCK_DID_F192,           10u  },   /* DID F192 */
    { MINIFEE_BLOCK_DID_F194,           10u  },   /* DID F194 */
    { MINIFEE_BLOCK_DID_F198,           11u  },   /* DID F198 */
    { MINIFEE_BLOCK_DID_F1A0,           5u   },   /* DID F1A0 */
    { MINIFEE_BLOCK_DID_F1A1,           5u   },   /* DID F1A1 */
    { MINIFEE_BLOCK_DID_F1A2,           8u   },   /* DID F1A2 */
    { MINIFEE_BLOCK_DID_F1A5,           3u   },   /* DID F1A5 */
    { MINIFEE_BLOCK_DID_F1A8,           20u  },   /* DID F1A8 */
    { MINIFEE_BLOCK_DID_F1A9,           5u   },   /* DID F1A9 */
    { MINIFEE_BLOCK_DID_F1AA,           5u   },   /* DID F1AA */
    { MINIFEE_BLOCK_DID_F130,           32u  },   /* DID F130 */
    { MINIFEE_BLOCK_DID_F1B5,           5u   },   /* DID F1B5 */
    { MINIFEE_BLOCK_DID_F1B6,           5u   },   /* DID F1B6 */
    { MINIFEE_BLOCK_DID_AFF1,           458u },   /* DID AFF1 */
    { MINIFEE_BLOCK_DID_AFF2,           1u   },   /* DID AFF2 */
    { MINIFEE_BLOCK_DID_AFF5,           1u   },   /* DID AFF5 */
    { MINIFEE_BLOCK_DID_AFFC,           2u   },   /* DID AFFC */
    { MINIFEE_BLOCK_DID_AFFD,           1u   },   /* DID AFFD */
    { MINIFEE_BLOCK_DID_AFFE,           1u   },   /* DID AFFE */
    { MINIFEE_BLOCK_DID_AFFF,           1u   },   /* DID AFFF */
    { MINIFEE_BLOCK_DID_A333,           112u },   /* DID A333 */
    { MINIFEE_BLOCK_DID_A444,           21u  },   /* DID A444 */
    { MINIFEE_BLOCK_DID_A555,           16u  },   /* DID A555 */
    { MINIFEE_BLOCK_VSS_DATA,           880u }    /* 车速信号数据：880B */
};

/* Cluster 配置表（条目数须与 MiniFee_Cfg.h 中 MINIFEE_CLUSTER_COUNT 一致） */
const MiniFee_ClusterConfigType MiniFee_ClusterConfig[MINIFEE_CLUSTER_COUNT] =
{
    { 0xFF200000UL, 0x2000UL },   /* Cluster 0 */
    { 0xFF202000UL, 0x2000UL }    /* Cluster 1 */
};