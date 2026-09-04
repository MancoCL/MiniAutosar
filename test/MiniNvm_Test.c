/**
 * \file MiniNvm_Test.c
 * \brief MiniNvm 单元测试（宿主机）
 *
 * 块数量与各块大小均由测试自备配置表传入，验证动态配置语义。
 */
#include "Test_Common.h"
#include "MiniNvm.h"
#include "MiniFee.h"
#include "FlashDrv_Stub.h"
#include "MiniNvm_Cfg.h"
#include "MiniFee_Cfg.h"
#include <string.h>

/* ---- 测试自备块配置（8 块，各块大小不同） ---- */
#define TEST_NUM_BLOCKS  ((uint16)8)

/* 各块 size: 0x80+0x40+0xF0+0x20+0x80+0x10+0x60+0x40 = 768 */
static MiniNvm_BlockConfigType testBlockCfg[TEST_NUM_BLOCKS] = {
    {0, 0x80, 0, TRUE, TRUE},
    {1, 0x40, 0, TRUE, TRUE},
    {2, 0xF0, 0, TRUE, TRUE},
    {3, 0x20, 0, TRUE, TRUE},
    {4, 0x80, 0, TRUE, TRUE},
    {5, 0x10, 0, TRUE, TRUE},
    {6, 0x60, 0, TRUE, TRUE},
    {7, 0x40, 0, TRUE, TRUE},
};
static uint8 testRam[768];

/* 测试缓冲按页数据区大小（= 块大小上限） */
static uint8 wbuf[MINIFEE_PAGE_DATA_SIZE];
static uint8 rbuf[MINIFEE_PAGE_DATA_SIZE];

/* 便捷 Init 包装 */
static void testInit(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam));
    (void)MiniNvm_ReadAll();
}

/* 按 size 填充测试模式 */
static void pat(uint8 *buf, uint16 size, uint8 seed)
{
    uint16 i;
    for (i = 0u; i < size; i++)
    {
        buf[i] = (uint8)(seed + (uint8)i);
    }
}

/* TC-N-01: Init + ReadAll 首启；未 ReadAll 读返回未初始化（P0 #11/#13） */
static void tc_init_readall_firstboot(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam)) == E_OK, "Init");
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

/* TC-N-02: WriteBlock 只更新 RAM（不落 Flash）（P0 #13） */
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

/* TC-N-03: WriteAll 持久化 + 重启恢复（P0 #13） */
static void tc_writeall_persist(void)
{
    uint16 sz;
    testInit();
    sz = MiniNvm_GetBlockSize(0u);
    pat(wbuf, sz, 0xB1);
    CHECK(MiniNvm_WriteBlock(0u, wbuf) == E_OK, "WriteBlock 0");
    CHECK(MiniNvm_WriteAll() == E_OK, "WriteAll");
    /* 模拟重启 */
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam)) == E_OK, "reboot Init");
    CHECK(MiniNvm_ReadAll() == E_OK, "reboot ReadAll");
    CHECK(MiniNvm_ReadBlock(0u, rbuf) == E_OK, "read back after reboot");
    pat(wbuf, sz, 0xB1);
    CHECK(memcmp(rbuf, wbuf, sz) == 0, "value survives reboot");
}

/* TC-N-04: EraseNvBlock + WriteAll 真擦除（P0 #11） */
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
    /* 重启后块 1 应为 NOT_FOUND */
    FlashDrv_Stub_Reset();
    (void)MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam));
    (void)MiniNvm_ReadAll();
    CHECK(MiniNvm_ReadBlock(1u, rbuf) == E_NOT_OK, "erased block -> E_NOT_OK after reboot");
}

/* TC-N-05: CRC 损坏经 ReadAll 记入错误状态（P0 #10/#11） */
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
    /* 重启 ReadAll 应识别为 CRC 错误 */
    FlashDrv_Stub_Reset();
    (void)MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam));
    (void)MiniNvm_ReadAll();
    {
        MiniNvm_ErrorStatusType es = 0u;
        CHECK(MiniNvm_GetErrorStatus(2u, &es) == E_OK, "get error status block2");
        CHECK((es & MININVM_ERR_CRC) != 0u, "CRC flag set");
    }
    CHECK(MiniNvm_ReadBlock(2u, rbuf) == E_NOT_OK, "corrupt block read -> E_NOT_OK");
}

/* TC-N-06: WriteAll 部分失败传播 + dirty 保留 + 重试恢复（P0 #11） */
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

/* TC-N-07: 多块混合写 + 重启校验（P0 #13/#15），验证各块变长 size */
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
    FlashDrv_Stub_Reset();
    (void)MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam));
    (void)MiniNvm_ReadAll();
    for (k = 0; k < 5; k++)
    {
        pat(wbuf, sizes[k], seeds[k]);
        CHECK(MiniNvm_ReadBlock((uint16)k, rbuf) == E_OK, "ReadBlock multi");
        CHECK(memcmp(rbuf, wbuf, sizes[k]) == 0, "value matches multi (var size)");
    }
}

/* TC-N-08: 变长块大小查询 + 块数查询 + 边界校验 */
static void tc_variable_block_size(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam)) == E_OK, "Init");
    /* 块 0 = 0x80, 块 1 = 0x40, 块 2 = 0xF0 — 各不相同 */
    CHECK(MiniNvm_GetBlockSize(0u) == 0x80u, "block0 size == 0x80");
    CHECK(MiniNvm_GetBlockSize(1u) == 0x40u, "block1 size == 0x40");
    CHECK(MiniNvm_GetBlockSize(2u) == 0xF0u, "block2 size == 0xF0");
    CHECK(MiniNvm_GetNumBlocks() == TEST_NUM_BLOCKS, "num blocks");
    /* 越界返回 0 */
    CHECK(MiniNvm_GetBlockSize(TEST_NUM_BLOCKS) == 0u, "invalid block -> size 0");
}

/* TC-N-09: Init 参数非法（NULL/0/超上限/RAM 不足） */
static void tc_init_param(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniNvm_Init(NULL_PTR, TEST_NUM_BLOCKS, testRam, (uint16)sizeof(testRam)) == E_NOT_OK, "NULL cfg");
    CHECK(MiniNvm_Init(testBlockCfg, 0u, testRam, (uint16)sizeof(testRam)) == E_NOT_OK, "numBlocks=0");
    CHECK(MiniNvm_Init(testBlockCfg, (uint16)(MININVM_MAX_NUM_BLOCKS + 1u), testRam, (uint16)sizeof(testRam)) == E_NOT_OK, "numBlocks>MAX");
    /* RAM 缓冲不足 */
    CHECK(MiniNvm_Init(testBlockCfg, TEST_NUM_BLOCKS, testRam, 1u) == E_NOT_OK, "RAM too small");
}

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
