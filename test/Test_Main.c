/**
 * \file Test_Main.c
 * \brief 宿主机测试入口
 */
#include "Test_Common.h"

int g_pass = 0;
int g_fail = 0;

int main(void)
{
    printf("== MiniFee tests ==\n");
    run_minifee_tests();
    printf("== MiniNvm tests ==\n");
    run_mininvm_tests();
    printf("\n----------------------------------------\n");
    printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
