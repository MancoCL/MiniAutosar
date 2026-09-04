/**
 * \file MiniFee_Test.c
 * \brief MiniFee 单元测试（宿主机）
 *
 * 块数量运行时传入（非编译期固定），测试用 8 块。
 */
#include "Test_Common.h"
#include "MiniFee.h"
#include "FlashDrv_Stub.h"
#include "MiniFee_Cfg.h"
#include <string.h>

#define TEST_NUM_BLOCKS  ((uint16)8)

static uint8 wrbuf[MINIFEE_PAGE_DATA_SIZE];
static uint8 rdbuf[MINIFEE_PAGE_DATA_SIZE];

static void fill_pattern(uint8 *buf, uint16 len, uint8 seed)
{
    uint16 i;
    for (i = 0u; i < len; i++)
    {
        buf[i] = (uint8)(seed + (uint8)i);
    }
}

/* TC-F-01: 全 0xFF 首启，读取未写过块返回 NOT_FOUND（P0 #9/#12） */
static void tc_first_boot(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniFee_Init(TEST_NUM_BLOCKS) == MINIFEE_OK, "Init first boot");
    CHECK(MiniFee_ReadBlock(0u, rdbuf, NULL_PTR) == MINIFEE_ERR_NOT_FOUND, "read unwritten block -> NOT_FOUND");
    CHECK(MiniFee_GetClusterCount() == MINIFEE_CLUSTER_NUM, "cluster count");
    CHECK(MiniFee_GetPageDataSize() == MINIFEE_PAGE_DATA_SIZE, "page data size");
    CHECK(MiniFee_GetNumBlocks() == TEST_NUM_BLOCKS, "num blocks");
}

/* TC-F-02: 正常写读（P0 #5/#10） */
static void tc_write_read_basic(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x10);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write block 0");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read block 0");
        CHECK(dl == 128u, "data len");
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "data matches");
    }
}

/* TC-F-03: 块更新作废旧页，仅一份有效（P0 #5/#6） */
static void tc_block_update(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x20);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write v1");
    fill_pattern(wrbuf, 128u, 0x30);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write v2");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read after update");
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "latest value v2");
    }
}

/* TC-F-04: 写满 cluster 触发 GC + 磨损轮转（P0 #6/#7）
 * Model A：所有写落到 active cluster；写满后 GC 把有效页搬到备用 cluster，擦除原 cluster。
 * 用 block1/block2 各写一次 + block0 重复写产生垃圾，填满 active(32 页) 后再写触发 GC。 */
static void tc_gc_and_wear(void)
{
    uint16 i;
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x11);
    CHECK(MiniFee_WriteBlock(1u, wrbuf, 128u) == MINIFEE_OK, "write block1");
    fill_pattern(wrbuf, 128u, 0x22);
    CHECK(MiniFee_WriteBlock(2u, wrbuf, 128u) == MINIFEE_OK, "write block2");
    /* block0 写 (pagesPerCluster-2) 次以填满 active cluster（共 2 + (P-2) = P 页） */
    for (i = 0u; i < (uint16)(MINIFEE_PAGES_PER_CLUSTER - 2u); i++)
    {
        fill_pattern(wrbuf, 128u, (uint8)(0x30u + (uint8)(i & 0x0Fu)));
        CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "fill block0");
    }
    /* 再写一次 block0：active 已满，触发 GC（搬运 block1/block2/block0(latest) 到备用 cluster） */
    fill_pattern(wrbuf, 128u, 0x77);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write triggers GC");
    /* 校验：所有 live 块经 GC 后仍可读且为最新值 */
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(1u, rdbuf, &dl) == MINIFEE_OK, "read block1 after GC");
        fill_pattern(wrbuf, 128u, 0x11);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "block1 value preserved");
        CHECK(MiniFee_ReadBlock(2u, rdbuf, &dl) == MINIFEE_OK, "read block2 after GC");
        fill_pattern(wrbuf, 128u, 0x22);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "block2 value preserved");
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read block0 after GC");
        fill_pattern(wrbuf, 128u, 0x77);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "block0 latest value");
    }
}

/* TC-F-05: CRC 损坏识别（P0 #10） */
static void tc_crc_corrupt(void)
{
    uint16 dl = 0u;
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x40);
    CHECK(MiniFee_WriteBlock(1u, wrbuf, 128u) == MINIFEE_OK, "write block1");
    /* 直接破坏数据区首字节（绕过写约束） */
    FlashDrv_Stub_SetByte(0u + 10u, (uint8)(FlashDrv_Stub_GetByte(0u + 10u) ^ 0xFFu));
    /* 重新扫描后读取应报告 CRC 错误 */
    CHECK(MiniFee_Init(TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan");
    CHECK(MiniFee_ReadBlock(1u, rdbuf, &dl) == MINIFEE_ERR_CRC, "CRC corruption detected");
}

/* TC-F-06: EraseBlock 作废 + 幂等（P0 #11 erase） */
static void tc_erase_block(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x50);
    CHECK(MiniFee_WriteBlock(2u, wrbuf, 128u) == MINIFEE_OK, "write block2");
    CHECK(MiniFee_EraseBlock(2u) == MINIFEE_OK, "erase block2");
    CHECK(MiniFee_ReadBlock(2u, rdbuf, NULL_PTR) == MINIFEE_ERR_NOT_FOUND, "read erased -> NOT_FOUND");
    CHECK(MiniFee_EraseBlock(2u) == MINIFEE_OK, "erase idempotent");
}

/* TC-F-07: 掉电丢"最新一页"（P0 #9）——提交前掉电，恢复后回到上一版本 */
static void tc_power_loss_latest(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x01);
    CHECK(MiniFee_WriteBlock(3u, wrbuf, 128u) == MINIFEE_OK, "write v1");
    /* 让下一次 status 写（提交）失败，模拟掉电 */
    FlashDrv_Stub_FailStatusWrites(1u);
    fill_pattern(wrbuf, 128u, 0x02);
    CHECK(MiniFee_WriteBlock(3u, wrbuf, 128u) == MINIFEE_ERR_FLASH, "write v2 lost at commit");
    /* 重新扫描恢复：v2 半写页被丢弃，应读回 v1 */
    CHECK(MiniFee_Init(TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan after power loss");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(3u, rdbuf, &dl) == MINIFEE_OK, "read after loss");
        fill_pattern(wrbuf, 128u, 0x01);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "reverted to v1 (latest lost)");
    }
}

/* TC-F-08: 参数非法 */
static void tc_param_errors(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(TEST_NUM_BLOCKS);
    CHECK(MiniFee_WriteBlock(TEST_NUM_BLOCKS, wrbuf, 1u) == MINIFEE_ERR_PARAM, "block oob");
    CHECK(MiniFee_WriteBlock(0u, NULL_PTR, 1u) == MINIFEE_ERR_PARAM, "null src");
    CHECK(MiniFee_WriteBlock(0u, wrbuf, (uint16)(MINIFEE_PAGE_DATA_SIZE + 1u)) == MINIFEE_ERR_PARAM, "len too big");
    CHECK(MiniFee_ReadBlock(0u, NULL_PTR, NULL_PTR) == MINIFEE_ERR_PARAM, "null dest");
}

/* TC-F-09: Init 参数校验（块数 0 / 超上限 / 超 pagesPerCluster） */
static void tc_init_param(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniFee_Init(0u) == MINIFEE_ERR_PARAM, "numBlocks=0 rejected");
    CHECK(MiniFee_Init((uint16)(MINIFEE_MAX_NUM_BLOCKS + 1u)) == MINIFEE_ERR_PARAM, "numBlocks>MAX rejected");
    /* pagesPerCluster=32, 33 块应被拒 */
    CHECK(MiniFee_Init(33u) == MINIFEE_ERR_PARAM, "numBlocks>=pagesPerCluster rejected");
}

void run_minifee_tests(void)
{
    tc_first_boot();
    tc_write_read_basic();
    tc_block_update();
    tc_gc_and_wear();
    tc_crc_corrupt();
    tc_erase_block();
    tc_power_loss_latest();
    tc_param_errors();
    tc_init_param();
}
