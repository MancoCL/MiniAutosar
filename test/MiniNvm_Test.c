/**
 * \file MiniNvm_Test.c
 * \brief MiniNvm 单元测试（宿主机）
 * \details 用例编号 TC-N-01~TC-N-09，用例表与预期见 docs/05_test_plan.md §3；
 *          块数量、块表和 RAM mirror 来自 MiniNvm_Cfg.h，验证静态配置语义。
 *          重启模拟 = 不调 FlashDrv_Stub_Reset，直接再调 MiniNvm_Init + ReadAll（docs/05 §1）。
 */
#include "Test_Common.h"
#include "MiniNvm.h"
#include "MiniFee.h"
#include "FlashDrv_Stub.h"
#include "MiniNvm_Cfg.h"
#include "MiniFee_Cfg.h"
#include <string.h>

/** 测试读写缓冲（按页数据区大小 = 块大小上限）。 */
static uint8 wbuf[MINIFEE_PAGE_DATA_SIZE];
static uint8 rbuf[MINIFEE_PAGE_DATA_SIZE];

/**
 * \brief 便捷初始化包装：全新首启（Stub_Reset 整片擦除）+ MiniNvm_Init + ReadAll。
 */
static void testInit(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniNvm_Init();
    (void)MiniNvm_ReadAll();
}

/**
 * \brief 以 seed+i 逐字节填充确定性测试模式。
 * \param[out] buf 目标缓冲
 * \param[in] size 填充字节数
 * \param[in] seed 起始种子
 */
static void pat(uint8 *buf, uint16 size, uint8 seed)
{
    uint16 i;
    for (i = 0u; i < size; i++)
    {
        buf[i] = (uint8)(seed + (uint8)i);
    }
}

/** \brief TC-N-01：Init + ReadAll 首启；未 ReadAll 前读取返回未初始化错误。\req P0 #11、#13 */
static void tc_init_readall_firstboot(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init() == E_OK, "Init");
    /* 未 ReadAll 前读取应失败（UNINIT） */
    CHECK(MiniNvm_ReadBlock(0u, rbuf) == E_NOT_OK, "read before ReadAll -> E_NOT_OK");
    {
        MiniNvm_ErrorStatusType es = 0u;
        CHECK(MiniNvm_GetErrorStatus(0u, &es) == E_OK, "get error status");
        CHECK((es & MININVM_ERR_UNINIT) != 0u, "UNINIT flag set");
    }
    CHECK(MiniNvm_ReadAll() == E_OK, "ReadAll first boot");
    /* 首启无数据：块 INVALID，读取返回 E_NOT_OK */
    CHECK(MiniNvm_ReadBlock(0u, rbuf) == E_NOT_OK, "read first-boot block -> E_NOT_OK");
}

/** \brief TC-N-02：WriteBlock 只更新 RAM（不落 Flash，Flash 字节保持 0xFF）。\req P0 #13 */
static void tc_writeblock_ram_only(void)
{
    uint16 sz;
    testInit();
    sz = MiniNvm_GetBlockSize(0u);
    pat(wbuf, sz, 0xA0);
    CHECK(MiniNvm_WriteBlock(0u, wbuf) == E_OK, "WriteBlock");
    CHECK(MiniNvm_ReadBlock(0u, rbuf) == E_OK, "ReadBlock from RAM");
    CHECK(memcmp(rbuf, wbuf, sz) == 0, "RAM mirror matches");
    /* Flash 未写入：直接查 MiniFee 应为 NOT_FOUND */
    CHECK(MiniFee_ReadBlock(0u, rbuf, NULL_PTR) == MINIFEE_ERR_NOT_FOUND, "Flash not touched");
}

/** \brief TC-N-03：WriteAll 持久化 + 重启恢复（不调 Stub_Reset，直接再 Init + ReadAll）。\req P0 #13 */
static void tc_writeall_persist(void)
{
    uint16 sz;
    testInit();
    sz = MiniNvm_GetBlockSize(0u);
    pat(wbuf, sz, 0xB1);
    CHECK(MiniNvm_WriteBlock(0u, wbuf) == E_OK, "WriteBlock 0");
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll");
    /* 模拟重启：不调 FlashDrv_Stub_Reset（会整片擦除），直接再 Init+ReadAll */
    CHECK(MiniNvm_Init() == E_OK, "reboot Init");
    CHECK(MiniNvm_ReadAll() == E_OK, "reboot ReadAll");
    CHECK(MiniNvm_ReadBlock(0u, rbuf) == E_OK, "read back after reboot");
    pat(wbuf, sz, 0xB1);
    CHECK(memcmp(rbuf, wbuf, sz) == 0, "value survives reboot");
}

/** \brief TC-N-04：EraseNvBlock + WriteAll 真擦除（重启后该块仍 INVALID，其余块完好）。\req P0 #11 */
static void tc_erase_then_writeall(void)
{
    uint16 sz;
    testInit();
    sz = MiniNvm_GetBlockSize(1u);
    pat(wbuf, sz, 0xC2);
    (void)MiniNvm_WriteBlock(1u, wbuf);
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll #1");
    CHECK(MiniNvm_EraseNvBlock(1u) == E_OK, "EraseNvBlock");
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll #2 (erase)");
    /* 重启后块 1 应为 NOT_FOUND（不调 Reset，保留 Flash 内容） */
    (void)MiniNvm_Init();
    (void)MiniNvm_ReadAll();
    CHECK(MiniNvm_ReadBlock(1u, rbuf) == E_NOT_OK, "erased block -> E_NOT_OK after reboot");
}

/** \brief TC-N-05：CRC 损坏经 ReadAll 记入错误状态，读镜像返回 E_NOT_OK。\req P0 #10、#11 */
static void tc_crc_error_status(void)
{
    uint16 sz;
    testInit();
    sz = MiniNvm_GetBlockSize(2u);
    pat(wbuf, sz, 0xD3);
    (void)MiniNvm_WriteBlock(2u, wbuf);
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll");
    /* 破坏块 2 的数据区首字节（首个写块位于地址 0 的数据区 offset 10） */
    FlashDrv_Stub_SetByte(10u, (uint8)(FlashDrv_Stub_GetByte(10u) ^ 0xFFu));
    /* 重启 ReadAll 应识别为 CRC 错误（不调 Reset，保留破坏的字节） */
    (void)MiniNvm_Init();
    (void)MiniNvm_ReadAll();
    {
        MiniNvm_ErrorStatusType es = 0u;
        CHECK(MiniNvm_GetErrorStatus(2u, &es) == E_OK, "get error status block2");
        CHECK((es & MININVM_ERR_CRC) != 0u, "CRC flag set");
    }
    CHECK(MiniNvm_ReadBlock(2u, rbuf) == E_NOT_OK, "corrupt block read -> E_NOT_OK");
}

/** \brief TC-N-06：WriteAll 部分失败错误传播 + dirty 保留 + 重试后恢复完整。\req P0 #11 */
static void tc_writeall_partial_fail(void)
{
    uint16 sz0, sz1;
    testInit();
    sz0 = MiniNvm_GetBlockSize(0u);
    sz1 = MiniNvm_GetBlockSize(1u);
    pat(wbuf, sz0, 0xE4);
    (void)MiniNvm_WriteBlock(0u, wbuf);
    pat(wbuf, sz1, 0xE5);
    (void)MiniNvm_WriteBlock(1u, wbuf);
    /* 注入一次写失败，令 WriteAll 中第一块写入失败 */
    FlashDrv_Stub_FailAnyWrites(1u);
    CHECK(MiniNvm_WriteAll() == E_NOT_OK, "WriteAll partial fail");
    {
        MiniNvm_ErrorStatusType es = 0u;
        (void)MiniNvm_GetErrorStatus(0u, &es);
        CHECK((es & MININVM_ERR_WRITE) != 0u, "WRITE flag on block0");
    }
    /* 清除故障后重试 WriteAll 应成功 */
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll retry ok");
}

/** \brief TC-N-07：多块混合写 + 重启校验，验证各块变长 size 互不串扰。\req P0 #13、#15 */
static void tc_multi_block(void)
{
    uint8 seeds[5] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint16 sizes[5];
    int k;
    testInit();
    for (k = 0; k < 5; k++)
    {
        sizes[k] = MiniNvm_GetBlockSize((uint16)k);
        pat(wbuf, sizes[k], seeds[k]);
        CHECK(MiniNvm_WriteBlock((uint16)k, wbuf) == E_OK, "WriteBlock multi");
    }
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll multi");
    /* 模拟重启：不调 Reset，保留 Flash 内容 */
    (void)MiniNvm_Init();
    (void)MiniNvm_ReadAll();
    for (k = 0; k < 5; k++)
    {
        pat(wbuf, sizes[k], seeds[k]);
        CHECK(MiniNvm_ReadBlock((uint16)k, rbuf) == E_OK, "ReadBlock multi");
        CHECK(memcmp(rbuf, wbuf, sizes[k]) == 0, "value matches multi (var size)");
    }
}

/** \brief TC-N-08：变长块大小查询 + 块数查询 + 越界边界校验。\req P0 #15 */
static void tc_variable_block_size(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init() == E_OK, "Init");
    /* 块 0 = 0x80, 块 1 = 0x40, 块 2 = 0xF0 — 各不相同 */
    CHECK(MiniNvm_GetBlockSize(0u) == 0x80u, "block0 size == 0x80");
    CHECK(MiniNvm_GetBlockSize(1u) == 0x40u, "block1 size == 0x40");
    CHECK(MiniNvm_GetBlockSize(2u) == 0xF0u, "block2 size == 0xF0");
    CHECK(MiniNvm_GetNumBlocks() == MININVM_MAX_NUM_BLOCKS, "num blocks from enum");
    /* 越界返回 0 */
    CHECK(MiniNvm_GetBlockSize(MININVM_MAX_NUM_BLOCKS) == 0u, "invalid block -> size 0");
}

/**
 * \brief TC-N-09：枚举末项自动提供块数；配置镜像由 Init 自动使用；
 *        Σ GetBlockSize = MININVM_RAM_MIRROR_SIZE（镜像容量自动计算正确）。
 * \req P0 #15
 */
static void tc_init_param(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init() == E_OK, "static config Init");
    CHECK(MiniNvm_GetNumBlocks() == (uint16)MININVM_MAX_NUM_BLOCKS, "enum count matches config");
    {
        uint16 total = 0u;
        uint16 b;
        for (b = 0u; b < MiniNvm_GetNumBlocks(); b++)
        {
            total = (uint16)(total + MiniNvm_GetBlockSize(b));
        }
        CHECK(total == MININVM_RAM_MIRROR_SIZE, "mirror size == sum of block sizes");
    }
}

/** \brief 依次运行 TC-N-01~TC-N-09。 */
void run_mininvm_tests(void)
{
    tc_init_readall_firstboot();
    tc_writeblock_ram_only();
    tc_writeall_persist();
    tc_erase_then_writeall();
    tc_crc_error_status();
    tc_writeall_partial_fail();
    tc_multi_block();
    tc_variable_block_size();
    tc_init_param();
}
