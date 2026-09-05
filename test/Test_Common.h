/**
 * \file Test_Common.h
 * \brief 测试框架公共定义
 * \details 无框架宿主测试基础设施：CHECK 断言宏累计 g_pass/g_fail 计数，
 *          单一二进制全量运行所有用例（用例表见 docs/05_test_plan.md）。
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

/** 通过断言计数（Test_Main.c 定义）。 */
extern int g_pass;
/** 失败断言计数（Test_Main.c 定义）；进程退出码由其是否为 0 决定。 */
extern int g_fail;

/** \brief 断言宏：条件为真累计 g_pass；为假打印文件/行/函数/消息并累计 g_fail。 */
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("[FAIL] %s:%d %s: %s\n", __FILE__, __LINE__, __func__, msg); } \
} while (0)

/** \brief 依次运行 MiniFee 全部用例（TC-F-01~09，MiniFee_Test.c）。 */
void run_minifee_tests(void);
/** \brief 依次运行 MiniNvm 全部用例（TC-N-01~09，MiniNvm_Test.c）。 */
void run_mininvm_tests(void);

#endif /* TEST_COMMON_H */
