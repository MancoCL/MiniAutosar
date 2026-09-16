/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniNvm_Test.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniNvm 开发阶段功能自检接口与结果结构（破坏性，仅测试用）。
 **
 **  @note               : 自检会擦除并重写数据 Flash 区域，禁止在量产流程中调用。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      同步接口变更，移除失败恢复用例宏
 **
 ***********************************************************************************************************************/

/* ================================================== module overview =============================================== */
/**
 * @brief           开发阶段破坏性自检说明（禁止在量产流程中调用）。
 * --------------------------------------------------------------------------------------------
* 开发阶段破坏性自检：会擦除并重写数据 Flash 区域，禁止在量产流程中调用。
* MiniNvm_Test() 依次执行下列用例并汇总结果，每个用例覆盖一组接口/异常路径：
*   PARAM       参数与未初始化拒绝
 *   BLANK       空白 Flash 读取无记录块应返回全零默认值
*   BASIC       单块写后读回一致
*   SOURCE_COPY 写请求入队后源缓冲可复用（验证入队快照）
*   ALL         WriteAll / ReadAll 全块一致性
 *   CANCEL      取消作业后结果置失败
 *   ROTATE      多次整块重写触发簇迁移/翻页，验证数据保全与 Generation 递增
* 注意：自检必须按规定顺序显式驱动 MiniFlsIf_MainFunction -> MiniFee_MainFunction ->
*       MiniNvm_MainFunction。
***********************************************************************************************/

#ifndef MININVM_TEST_H_
#define MININVM_TEST_H_

#include "Common.h"
#include "MiniNvm.h"

/* 自检用例编号（同时作为失败时上报的 FailCaseId） */
#define MININVM_TEST_CASE_PARAM       (1u)   /* 参数/未初始化校验 */
#define MININVM_TEST_CASE_BLANK       (2u)   /* 空白读取返回全零默认值 */
#define MININVM_TEST_CASE_BASIC       (3u)   /* 单块读写 */
#define MININVM_TEST_CASE_SOURCE_COPY  (4u)  /* 入队后源缓冲复用 */
#define MININVM_TEST_CASE_ALL         (5u)   /* 多块读写 */
#define MININVM_TEST_CASE_CANCEL      (6u)   /* 取消 */
#define MININVM_TEST_CASE_ROTATE      (7u)   /* 多次整块重写触发簇迁移/翻页，验证数据保全与 Generation 递增 */

/* 自检结果：总体结果、当前/失败用例与失败点，便于定位问题 */
typedef struct
{
    uint8 OverallResult;    /* 总体结果：E_OK 表示全部通过 */
    uint8 CurCase;          /* 当前执行的用例编号 */
    uint8 FailCaseId;       /* 失败用例编号 */
    uint8 FailStep;         /* 失败步骤（用例内的检查点编号） */
    uint16 FailOffset;      /* 数据不一致时的字节偏移 */
    uint8 ExpectByte;       /* 期望字节 */
    uint8 ActualByte;       /* 实际字节 */
    uint32 PassCount;       /* 通过用例数 */
    uint32 FailCount;       /* 失败用例数 */
} MiniNvm_TestResultType;

extern MiniNvm_TestResultType MiniNvm_TestResult;   /* 自检结果（可在线调试查看） */
extern uint8 MiniNvm_Test(void);                    /* 执行全部自检，E_OK 表示全部通过 */

#endif /* MININVM_TEST_H_ */
