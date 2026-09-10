/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniFlsIf.c
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniFee 异步 Flash 平台适配实现（当前映射 RH850 Fls MCAL）。
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

#include "MiniFlsIf.h"
#include "SchM_Fls.h"
#include "Fls.h"

uint8 MiniFlsIf_Init(void)
{
    Fls_Init(FlsConfigSet);
    return E_OK;
}

uint8 MiniFlsIf_DeInit(void)
{
    return E_OK;
}

uint8 MiniFlsIf_Read(uint32 offset, uint8* buf, uint32 len)
{
    return Fls_Read(offset, buf, len);
}

uint8 MiniFlsIf_Write(uint32 offset, const uint8* buf, uint32 len)
{
    return Fls_Write(offset, buf, len);
}

uint8 MiniFlsIf_Erase(uint32 offset, uint32 len)
{
    return Fls_Erase(offset, len);
}

MemIf_StatusType MiniFlsIf_GetStatus(void)
{
    return Fls_GetStatus();
}

MemIf_JobResultType MiniFlsIf_GetJobResult(void)
{
    return Fls_GetJobResult();
}

void MiniFlsIf_MainFunction(void)
{
    Fls_MainFunction();
}
