/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniFee.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniFee EEPROM 仿真接口，基于 Flash 驱动的非易失存储抽象层。
*                   按块号整块读写：块号由 MiniFee_BlockIdType 枚举定义，块大小由
*                   MiniFee_BlockConfig[] 逐块配置；追加式写入 + 新纪录遮蔽旧纪录 + 迁移反向
*                   扫描去重回收；无 Valid/Invalid 标志，无 CLOSED 二次编程，任何区域仅一次编程。
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

#ifndef MINIFEE_H_
#define MINIFEE_H_
#include "Common.h"
#include "MemM.h"
#include "MiniFee_Cfg.h"   /* 可变配置参数（块配置表、Cluster 配置表、地址基址、块枚举等） */

/*********************************************************************************************************************/
/* 格式常量（由存储格式与 Flash 编程特性决定：擦除后只允许一次编程，无 1→0 二次编程，不可配置）                            */
/*********************************************************************************************************************/
#define MINIFEE_CLUSTER_HEADER_SIZE        (8u)    /* Cluster 头大小：Magic(4)+Generation(3)+CheckSum(1) */
#define MINIFEE_BLOCK_HEADER_SIZE          (8u)    /* 块头大小：BlockNumber(1)+Length(2)+PrevOffset(4)+CheckSum(1)，无标志页 */
#define MINIFEE_ALIGN_LEN(len)             (((len) + (MINIFEE_VIRTUALPAGE_SIZE - 1u)) & ~(MINIFEE_VIRTUALPAGE_SIZE - 1u))  /* 数据长度按 Flash 最小写入单元对齐 */
#define MINIFEE_BLOCK_TOTAL_OF(len)        (MINIFEE_BLOCK_HEADER_SIZE + (len))   /* 块头+数据总大小（变长，随块长度变化） */

#define MINIFEE_ERASED_VALUE               (0xFFu)
#define MINIFEE_CLUSTER_MAGIC              (0xAA55AA44UL)   /* Cluster 头 Magic（4 字节），无 Status 字段 */

/*********************************************************************************************************************/
/* 块头结构（序列化为 8 字节，大端序）                                                                                     */
/* 字段说明：BlockNumber=块号（1B，=配置数组下标）；Length=该块逻辑数据长度（2B）；PrevOffset=本簇内上一条纪录块头偏移     */
/* （4B，0=无）；CheckSum=CRC-8（1B，多项式 0x07，输入为 BlockNumber+Length+PrevOffset）。数据固定紧随块头（+8）。         */
/*********************************************************************************************************************/
typedef struct
{
    uint8  BlockNumber;    /* 逻辑块号（= 配置数组下标，1 字节） */
    uint16 Length;         /* 该块逻辑数据长度（2 字节） */
    uint32 PrevOffset;     /* 上一条纪录块头在簇内偏移（0=本簇首条纪录），供迁移反向扫描 */
    uint8  CheckSum;       /* CRC-8（多项式 0x07） */
} MiniFee_BlockHeaderType;

/*********************************************************************************************************************/
/* 异步任务状态                                                                                                        */
/*********************************************************************************************************************/
typedef enum
{
    MINIFEE_STATUS_IDLE = 0,
    MINIFEE_STATUS_BUSY,
    MINIFEE_STATUS_OK,
    MINIFEE_STATUS_NOT_OK
} MiniFee_StatusType;

/*********************************************************************************************************************/
/* 对外接口                                                                                                           */
/*********************************************************************************************************************/
/* 异步模式：Read/Write 立即返回，调用方须周期调用 MiniFee_MainFunction 推进状态机，
   并用 MiniFee_GetStatus 查询结果。Fls 推进与喂狗由调用方在外部循环负责。
   Read/Write 按块号整块读写，blockNumber 为 MiniFee_BlockIdType 枚举，
   size 必须等于 MiniFee_BlockConfig[blockNumber].Length。 */
extern void              MiniFee_Init(void);
extern uint8             MiniFee_DeInit(void);
extern uint8             MiniFee_Read(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf);
extern uint8             MiniFee_Write(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf);
extern uint8             MiniFee_Cancel(void);
extern void              MiniFee_MainFunction(void);
extern MiniFee_StatusType   MiniFee_GetStatus(void);
#endif
