/**
 * \file FlashDrv_Stub.h
 * \brief Flash 驱动 RAM stub 测试桩接口（仅宿主测试用）
 *
 * 用 RAM 数组模拟 Flash，并提供故障注入钩子，用于在 PC 上编译运行模块测试。
 * 真实目标板请实现自己的 FlashDrv_xxx，不要使用本文件。
 */
#ifndef FLASHDRV_STUB_H
#define FLASHDRV_STUB_H

#include "Std_Types.h"

/** 复位 stub：将整片 Flash 置为 0xFF（擦除态），清空故障注入。每次"全新首启"前调用。 */
void FlashDrv_Stub_Reset(void);

/** 获取 Flash 原始字节（测试检视用）。 */
uint8 FlashDrv_Stub_GetByte(uint32 addr);

/** 直接设置 Flash 原始字节（绕过写约束，仅测试注入损坏用）。 */
void FlashDrv_Stub_SetByte(uint32 addr, uint8 val);

/** 让接下来的 n 次 status 写（2 字节写）失败，模拟"提交前掉电"。 */
void FlashDrv_Stub_FailStatusWrites(uint8 n);

/** 让接下来的 n 次任意 FlashDrv_Write 失败。 */
void FlashDrv_Stub_FailAnyWrites(uint8 n);

/** 让接下来的 n 次 FlashDrv_EraseCluster 失败。 */
void FlashDrv_Stub_FailErases(uint8 n);

#endif /* FLASHDRV_STUB_H */
