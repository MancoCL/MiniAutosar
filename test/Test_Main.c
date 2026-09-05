/**
 * \file Test_Main.c
 * \brief 宿主机测试入口
 * \details 调用 MiniFee/MiniNvm 两组用例后打印 PASS/FAIL 汇总；
 *          进程退出码 0 = 全部通过（g_fail == 0），1 = 存在失败。
 */
#include "Test_Common.h"

int g_pass = 0;
int g_fail = 0;

/** \brief 测试入口：全量运行两组用例并输出汇总。\return 0：全部通过；1：存在失败。 */
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
