/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm_Test.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniNvm 开发阶段功能自检接口与结果结构（破坏性，仅测试用）。
*
************************************************************************************************
*   限制         @: 自检会擦除并重写数据 Flash 区域，禁止在量产流程中调用。
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

#ifndef MININVM_TEST_H_
#define MININVM_TEST_H_

#include "Common.h"
#include "MiniNvm.h"

#define MININVM_TEST_CASE_PARAM       (1u)
#define MININVM_TEST_CASE_BLANK       (2u)
#define MININVM_TEST_CASE_BASIC       (3u)
#define MININVM_TEST_CASE_SOURCE_COPY  (4u)
#define MININVM_TEST_CASE_ALL         (5u)
#define MININVM_TEST_CASE_CANCEL      (6u)
#define MININVM_TEST_CASE_FAILURE     (7u)
#define MININVM_TEST_CASE_ROTATE      (8u)   /* 多次整块重写触发簇迁移/翻页，验证数据保全与 Generation 递增 */

typedef struct
{
    uint8 OverallResult;
    uint8 CurCase;
    uint8 FailCaseId;
    uint8 FailStep;
    uint16 FailOffset;
    uint8 ExpectByte;
    uint8 ActualByte;
    uint32 PassCount;
    uint32 FailCount;
} MiniNvm_TestResultType;

extern MiniNvm_TestResultType MiniNvm_TestResult;
extern uint8 MiniNvm_Test(void);

#endif /* MININVM_TEST_H_ */
