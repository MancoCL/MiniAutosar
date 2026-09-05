/**
 * \file FlashDrv_Stub.h
 * \brief Flash 驱动 RAM stub 测试桩接口（仅宿主测试用）
 * \details 用静态 RAM 数组模拟 Flash（容量 MINIFEE_TOTAL_CAPACITY），并提供故障注入钩子，
 *          用于在 PC 上编译运行模块测试。真实目标板请实现自己的 FlashDrv_xxx，不要使用本文件。
 *          重启/掉电模拟语义：FlashDrv_Init 不擦除（内容保留），全新首启才用 FlashDrv_Stub_Reset。
 */
#ifndef FLASHDRV_STUB_H
#define FLASHDRV_STUB_H

#include "Std_Types.h"

/**
 * \brief 复位 stub：将整片 Flash 置为 0xFF（擦除态），并清空全部故障注入计数。
 * \details 仅用于"全新首启"场景；模拟重启/掉电不得调用（内容须保留以验证恢复路径）。
 */
void FlashDrv_Stub_Reset(void);

/**
 * \brief 获取 Flash 原始字节（绕过驱动接口，测试检视用）。
 * \param[in] addr 字节地址 [0, MINIFEE_TOTAL_CAPACITY)
 * \return 该地址内容；越界返回 0xFF。
 */
uint8 FlashDrv_Stub_GetByte(uint32 addr);

/**
 * \brief 直接设置 Flash 原始字节（绕过 1→0 写约束，仅测试注入损坏用）。
 * \param[in] addr 字节地址 [0, MINIFEE_TOTAL_CAPACITY)
 * \param[in] val  写入值
 */
void FlashDrv_Stub_SetByte(uint32 addr, uint8 val);

/**
 * \brief 令接下来 n 次提交页写失败，模拟"提交前掉电"。
 * \details 识别约定：整页写（len == MINIFEE_PAGE_SIZE）且首 2 字节为 0x55 0x55
 *          （提交页 status=VALID 特征）的写视为提交页写。
 * \param[in] n 失败次数
 */
void FlashDrv_Stub_FailStatusWrites(uint8 n);

/**
 * \brief 令接下来 n 次任意 FlashDrv_Write 失败。
 * \param[in] n 失败次数
 */
void FlashDrv_Stub_FailAnyWrites(uint8 n);

/**
 * \brief 令接下来 n 次 FlashDrv_EraseCluster 失败。
 * \param[in] n 失败次数
 */
void FlashDrv_Stub_FailErases(uint8 n);

#endif /* FLASHDRV_STUB_H */
