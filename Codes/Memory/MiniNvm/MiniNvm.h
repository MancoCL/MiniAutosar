/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: Manco
************************************************************************************************
*   描述         @: MiniNvm 对外接口：管理 RAM Block、排队单块/多块异步请求并维护请求结果，
*                   实际存储由 MiniFee 完成。API 返回 E_OK 仅表示请求已接受，最终结果经
*                   MiniNvm_GetErrorStatus / MiniNvm_GetMultiJobStatus 查询。
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
*   V1.1       2026/09/16    Manco              N/A         重构：一次性读写、RAM 同步接口，移除默认值恢复
*
************************************************************************************************
* END_FILE_HDR*/

#ifndef MININVM_H_
#define MININVM_H_

#include "Common.h"
#include "MiniFee.h"
#include "MiniNvm_Cfg.h"

typedef enum
{
    MININVM_REQ_IDLE = 0u,
    MININVM_REQ_PENDING,
    MININVM_REQ_OK,
    MININVM_REQ_NOT_OK
} MiniNvm_RequestResultType;

void MiniNvm_Init(void);
void MiniNvm_DeInit(void);
uint8 MiniNvm_ReadBlock(uint8 blockId, uint8* dstPtr);
uint8 MiniNvm_WriteBlock(uint8 blockId, const uint8* srcPtr);
uint8 MiniNvm_ReadAll(void);
uint8 MiniNvm_WriteAll(void);
uint8 MiniNvm_CancelJobs(void);
uint8 MiniNvm_GetErrorStatus(uint8 blockId, MiniNvm_RequestResultType* resultPtr);
MiniNvm_RequestResultType MiniNvm_GetMultiJobStatus(void);
void MiniNvm_MainFunction(void);

/* 同步 RAM 访问（不触发 Flash 作业）：
   - GetRamBuffer 返回指定块（MiniFee 块号，从 0 开始）的 RAM buffer 地址；
   - ReadRam 从指定块 RAM buffer 同步读取 length 字节；
   - WriteRam 同步写入并置脏（不一致时），需后续 MiniNvm_WriteAll 落盘。 */
uint8* MiniNvm_GetRamBuffer(MiniFee_BlockIdType blockNumber);
uint8 MiniNvm_ReadRam(MiniFee_BlockIdType blockNumber, uint8* dstPtr, uint32 length);
uint8 MiniNvm_WriteRam(MiniFee_BlockIdType blockNumber, const uint8* srcPtr, uint32 length);

#endif /* MININVM_H_ */
