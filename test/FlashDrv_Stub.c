/**
 * \file FlashDrv_Stub.c
 * \brief Flash 驱动 RAM stub 实现（仅宿主测试用）
 * \details 用静态 RAM 数组模拟 Fee 管理区，遵循 Flash 写约束（只能 1→0，擦除恢复 0xFF）；
 *          提供故障注入：status 写失败（模拟提交前掉电）、任意写失败、擦除失败。
 *
 *          FlashDrv_Init 不擦除 Flash（模拟真实芯片上电不擦除），由 FlashDrv_Stub_Reset
 *          负责整片擦除——可在"掉电"后再次调用 MiniFee_Init/MiniNvm_Init 触发恢复流程
 *          而不丢失内容（重启模拟语义见 docs/05_test_plan.md §1）。
 */

#include "FlashDrv.h"
#include "FlashDrv_Stub.h"
#include "MiniFee_Cfg.h"
#include <string.h>

/** 模拟 Flash 的 RAM 数组（内容跨"重启"保留，仅 Stub_Reset 整片擦除）。 */
static uint8 flash[MINIFEE_TOTAL_CAPACITY];

/* ---- 故障注入计数（接口语义见 FlashDrv_Stub.h） ---- */
static uint8 failStatusWritesLeft = 0u;
static uint8 failAnyWritesLeft    = 0u;
static uint8 failEraseLeft        = 0u;

/** \brief 实现：整片循环置 0xFF 并清零全部故障注入计数。 */
void FlashDrv_Stub_Reset(void)
{
    uint32 i;
    for (i = 0u; i < (uint32)MINIFEE_TOTAL_CAPACITY; i++)
    {
        flash[i] = 0xFFu;
    }
    failStatusWritesLeft = 0u;
    failAnyWritesLeft    = 0u;
    failEraseLeft        = 0u;
}

/** \brief 实现：越界保护后返回数组内容。 */
uint8 FlashDrv_Stub_GetByte(uint32 addr)
{
    if (addr >= (uint32)MINIFEE_TOTAL_CAPACITY)
    {
        return 0xFFu;
    }
    return flash[addr];
}

/** \brief 实现：越界保护后直接赋值（绕过写约束）。 */
void FlashDrv_Stub_SetByte(uint32 addr, uint8 val)
{
    if (addr < (uint32)MINIFEE_TOTAL_CAPACITY)
    {
        flash[addr] = val;
    }
}

/* ---- 故障注入计数设置（接口语义见 FlashDrv_Stub.h） ---- */
void FlashDrv_Stub_FailStatusWrites(uint8 n) { failStatusWritesLeft = n; }
void FlashDrv_Stub_FailAnyWrites(uint8 n)    { failAnyWritesLeft = n; }
void FlashDrv_Stub_FailErases(uint8 n)        { failEraseLeft = n; }

/** \brief 实现：仅复位故障注入计数，不擦除内容（模拟真实芯片上电不擦除）。 */
FlashDrv_ReturnType FlashDrv_Init(void)
{
    /* 真实芯片上电不擦除 Flash；这里保留内容，仅复位故障计数 */
    failStatusWritesLeft = 0u;
    failAnyWritesLeft    = 0u;
    failEraseLeft        = 0u;
    return FLASH_OK;
}

/** \brief 实现：NULL/len=0 → PARAM；越界 → BOUNDARY；其余从 RAM 数组拷贝。 */
FlashDrv_ReturnType FlashDrv_Read(uint32 addr, uint16 len, uint8 *dest)
{
    uint16 i;
    if ((dest == NULL_PTR) || (len == 0u))
    {
        return FLASH_ERR_PARAM;
    }
    if ((addr > (uint32)MINIFEE_TOTAL_CAPACITY) || ((addr + (uint32)len) > (uint32)MINIFEE_TOTAL_CAPACITY))
    {
        return FLASH_ERR_BOUNDARY;
    }
    for (i = 0u; i < len; i++)
    {
        dest[i] = flash[addr + i];
    }
    return FLASH_OK;
}

/** \brief 实现：故障注入优先匹配（2 字节写视为 status 写）；逐位校验 1→0，
 *          违反 → PROG，通过则按 old&new 落盘。 */
FlashDrv_ReturnType FlashDrv_Write(uint32 addr, uint16 len, const uint8 *src)
{
    uint16 i;
    if ((src == NULL_PTR) || (len == 0u))
    {
        return FLASH_ERR_PARAM;
    }
    if ((addr > (uint32)MINIFEE_TOTAL_CAPACITY) || ((addr + (uint32)len) > (uint32)MINIFEE_TOTAL_CAPACITY))
    {
        return FLASH_ERR_BOUNDARY;
    }

    /* 故障注入：2 字节写视为 status 写（提交/作废），优先匹配 */
    if ((len == 2u) && (failStatusWritesLeft > 0u))
    {
        failStatusWritesLeft--;
        return FLASH_ERR_FAIL; /* 模拟提交前掉电 */
    }
    if (failAnyWritesLeft > 0u)
    {
        failAnyWritesLeft--;
        return FLASH_ERR_FAIL;
    }

    /* Flash 写约束：只能 1→0；若存在 0→1 则报 PROG */
    for (i = 0u; i < len; i++)
    {
        uint8 oldv = flash[addr + i];
        uint8 newv = src[i];
        if ((newv & (~oldv)) != 0u)
        {
            return FLASH_ERR_PROG;
        }
        flash[addr + i] = (uint8)(oldv & newv);
    }
    return FLASH_OK;
}

/** \brief 实现：故障注入优先；索引越界 → PARAM；整 cluster 置 0xFF。 */
FlashDrv_ReturnType FlashDrv_EraseCluster(uint8 clusterIdx)
{
    uint32 base;
    uint32 i;
    if (clusterIdx >= (uint8)MINIFEE_CLUSTER_NUM)
    {
        return FLASH_ERR_PARAM;
    }
    if (failEraseLeft > 0u)
    {
        failEraseLeft--;
        return FLASH_ERR_ERASE;
    }
    base = (uint32)clusterIdx * (uint32)MINIFEE_CLUSTER_SIZE;
    for (i = 0u; i < (uint32)MINIFEE_CLUSTER_SIZE; i++)
    {
        flash[base + i] = 0xFFu;
    }
    return FLASH_OK;
}

/** \brief 实现：返回与 MiniFee_Cfg.h 配置对齐的属性（写粒度 1 字节、cluster 原子擦除）。 */
FlashDrv_ReturnType FlashDrv_GetProperty(FlashDrv_PropertyType *prop)
{
    if (prop == NULL_PTR)
    {
        return FLASH_ERR_PARAM;
    }
    prop->pageSize         = (uint32)MINIFEE_PAGE_SIZE;
    prop->clusterSize      = (uint32)MINIFEE_CLUSTER_SIZE;
    prop->clusterCount     = (uint32)MINIFEE_CLUSTER_NUM;
    prop->totalCapacity     = (uint32)MINIFEE_TOTAL_CAPACITY;
    prop->writeGranularity  = 1u;
    prop->eraseAtomicity    = TRUE;
    return FLASH_OK;
}
