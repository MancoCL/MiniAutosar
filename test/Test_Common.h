/**
 * \file Test_Common.h
 * \brief 测试框架公共定义
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

extern int g_pass;
extern int g_fail;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("[FAIL] %s:%d %s: %s\n", __FILE__, __LINE__, __func__, msg); } \
} while (0)

void run_minifee_tests(void);
void run_mininvm_tests(void);

#endif /* TEST_COMMON_H */
