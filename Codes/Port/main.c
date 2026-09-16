/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : main.c
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境的可执行入口：驱动 MiniNvm_Test() 并打印结果。
 **
 **  @details            : 本程序把 MiniNvm / MiniFee / MiniFlsIf 三层与 RAM 仿真 Fls 一起链接为控制台程序，
 **                        可编译、运行与单步调试。运行即执行破坏性自检 MiniNvm_Test()，覆盖参数、空白、单块读写、
 **                        源数据快照、全量读写、取消与簇迁移/翻页等用例。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。
 **
 ***********************************************************************************************************************/

/* =================================================== inclusions =================================================== */
#include <stdio.h>
#include <string.h>

#include "Common.h"
#include "Fls_Sim.h"
#include "MiniNvm_Test.h"

/* ========================================== external function definitions ========================================= */
static const char* MiniAutosar_Host_CaseName(uint8 caseId)
{
    switch (caseId)
    {
        case MININVM_TEST_CASE_PARAM:       return "PARAM";
        case MININVM_TEST_CASE_BLANK:       return "BLANK";
        case MININVM_TEST_CASE_BASIC:       return "BASIC";
        case MININVM_TEST_CASE_SOURCE_COPY: return "SOURCE_COPY";
        case MININVM_TEST_CASE_ALL:         return "ALL";
        case MININVM_TEST_CASE_CANCEL:      return "CANCEL";
        case MININVM_TEST_CASE_ROTATE:      return "ROTATE";
        default:                            return "UNKNOWN";
    }
}

static void MiniAutosar_Host_PrintResult(void)
{
    (void)printf("------------------------------------------------------------\n");
    (void)printf("MiniNvm self-test report\n");
    (void)printf("  OverallResult : %s (E_OK=%u)\n",
                 (MiniNvm_TestResult.OverallResult == E_OK) ? "PASS" : "FAIL",
                 (unsigned int)MiniNvm_TestResult.OverallResult);
    (void)printf("  PassCount     : %lu\n", (unsigned long)MiniNvm_TestResult.PassCount);
    (void)printf("  FailCount     : %lu\n", (unsigned long)MiniNvm_TestResult.FailCount);
    (void)printf("  CurCase       : %s (%u)\n",
                 MiniAutosar_Host_CaseName(MiniNvm_TestResult.CurCase),
                 (unsigned int)MiniNvm_TestResult.CurCase);

    if (MiniNvm_TestResult.FailCount != 0u)
    {
        (void)printf("  FailCaseId    : %s (%u)\n",
                     MiniAutosar_Host_CaseName(MiniNvm_TestResult.FailCaseId),
                     (unsigned int)MiniNvm_TestResult.FailCaseId);
        (void)printf("  FailStep      : %u\n", (unsigned int)MiniNvm_TestResult.FailStep);
        (void)printf("  FailOffset    : %u\n", (unsigned int)MiniNvm_TestResult.FailOffset);
        (void)printf("  ExpectByte    : 0x%02X\n", (unsigned int)MiniNvm_TestResult.ExpectByte);
        (void)printf("  ActualByte    : 0x%02X\n", (unsigned int)MiniNvm_TestResult.ActualByte);
    }

    (void)printf("------------------------------------------------------------\n");
    (void)printf("Fls simulation counters\n");
    (void)printf("  ProgramViolations : %lu\n", (unsigned long)Fls_Sim_ProgramViolations);
    (void)printf("  OutOfRange        : %lu\n", (unsigned long)Fls_Sim_OutOfRange);
    (void)printf("  RejectedRequests  : %lu\n", (unsigned long)Fls_Sim_RejectedRequests);
    (void)printf("------------------------------------------------------------\n");
}

int main(int argc, char** argv)
{
    uint8 result;
    int   exitCode = 0;
    int   dump = 0;

    if ((argc > 1) && (strcmp(argv[1], "--dump") == 0))
    {
        dump = 1;
    }

    (void)printf("============================================================\n");
    (void)printf(" MiniAutosar host verification (Windows / RAM-simulated Fls)\n");
    (void)printf("============================================================\n");

    result = MiniNvm_Test();
    MiniAutosar_Host_PrintResult();

    if (dump != 0)
    {
        (void)printf("Simulated Flash image (0x0000 .. 0x%04lX)\n", (unsigned long)Fls_Sim_Size);
        Fls_Sim_Dump(0u, Fls_Sim_Size);
    }

    if (result != E_OK)
    {
        (void)printf("RESULT: FAIL\n");
        exitCode = 1;
    }
    else
    {
        (void)printf("RESULT: PASS\n");
    }

    return exitCode;
}

/*=======[E N D   O F   F I L E]===============================================*/
