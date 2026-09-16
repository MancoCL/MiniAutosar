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
*   作者         @: Manco
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
*   V1.0       2026/09/10    Manco              N/A         初版发布
*   V1.1       2026/09/16    Manco              N/A         补充实现说明注释
*
************************************************************************************************
* END_FILE_HDR*/

/**********************************************************************************************
* 实现说明：
*   本文件是三个存储模块中唯一与平台绑定的文件，只把 Fls MCAL 的接口原样转发出去，
*   不修改地址、长度与数据内容，也不做同步等待。移植到其他 Flash 驱动时只改本文件即可。
***********************************************************************************************/

#include "MiniFlsIf.h"
#include "SchM_Fls.h"
#include "Fls.h"

/* 初始化底层 Flash 驱动：用 Fls 配置集 FlsConfigSet 完成初始化，成功后固定返回 E_OK */
uint8 MiniFlsIf_Init(void)
{
    Fls_Init(FlsConfigSet);
    return E_OK;
}

/* 反初始化底层 Flash 驱动：当前为空实现（若目标平台需关时钟/控制器，在此补充） */
uint8 MiniFlsIf_DeInit(void)
{
    return E_OK;
}

/* 发起异步读取：直接转发 Fls_Read；返回值 E_OK 只表示请求被底层接受 */
uint8 MiniFlsIf_Read(uint32 offset, uint8* buf, uint32 len)
{
    return Fls_Read(offset, buf, len);
}

/* 发起异步写入：直接转发 Fls_Write；底层异步持有 buf 指针，调用后源缓冲不可被改写 */
uint8 MiniFlsIf_Write(uint32 offset, const uint8* buf, uint32 len)
{
    return Fls_Write(offset, buf, len);
}

/* 发起异步擦除：直接转发 Fls_Erase */
uint8 MiniFlsIf_Erase(uint32 offset, uint32 len)
{
    return Fls_Erase(offset, len);
}

/* 查询底层任务状态（含 MEMIF_BUSY / MEMIF_IDLE 等） */
MemIf_StatusType MiniFlsIf_GetStatus(void)
{
    return Fls_GetStatus();
}

/* 查询最近一次任务结果（MEMIF_JOB_OK / FAILED / PENDING / CANCELED 等） */
MemIf_JobResultType MiniFlsIf_GetJobResult(void)
{
    return Fls_GetJobResult();
}

/* 周期推进底层异步任务：转发 Fls_MainFunction，由系统调度器周期调用 */
void MiniFlsIf_MainFunction(void)
{
    Fls_MainFunction();
}
