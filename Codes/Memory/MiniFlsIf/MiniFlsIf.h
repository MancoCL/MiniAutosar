/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniFlsIf.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniFee 使用的异步 Flash 平台适配接口，屏蔽底层具体 Flash 驱动。
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

#ifndef MINIFLSIF_H_
#define MINIFLSIF_H_

#include "Common.h"
#include "MemIf_Types.h"

/* 初始化与反初始化 */
extern uint8 MiniFlsIf_Init(void);
extern uint8 MiniFlsIf_DeInit(void);

/* 异步 Flash 访问接口 */
extern uint8 MiniFlsIf_Read(uint32 offset, uint8* buf, uint32 len);
extern uint8 MiniFlsIf_Write(uint32 offset, const uint8* buf, uint32 len);
extern uint8 MiniFlsIf_Erase(uint32 offset, uint32 len);

/* 异步任务状态接口 */
extern MemIf_StatusType MiniFlsIf_GetStatus(void);
extern MemIf_JobResultType MiniFlsIf_GetJobResult(void);
extern void MiniFlsIf_MainFunction(void);

#endif /* MINIFLSIF_H_ */
