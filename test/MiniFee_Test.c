/**
 * \file MiniFee_Test.c
 * \brief MiniFee 单元测试（宿主机）
 * \details 用例编号 TC-F-01~TC-F-09，用例表与预期见 docs/05_test_plan.md §2；
 *          逐块 size 数组运行时传入 MiniFee_Init（8 块、默认均 128B；
 *          TC-F-02b 使用变长 size）。
 */
#include "Test_Common.h"
#include "MiniFee.h"
#include "FlashDrv_Stub.h"
#include "MiniFee_Cfg.h"
#include <string.h>

/** 测试块数（运行时传入 MiniFee_Init；须 > 0 且 ≤ MINIFEE_MAX_NUM_BLOCKS）。 */
#define TEST_NUM_BLOCKS  ((uint16)8u)

/** 默认测试块大小（= PAGE_SIZE 整数倍）。 */
#define TEST_BLOCK_SIZE  ((uint16)128u)

/** 默认槽页数 = size/PAGE_SIZE + 2（派生，勿手改）。 */
#define TEST_SLOT_PAGES  ((uint16)((TEST_BLOCK_SIZE / MINIFEE_PAGE_SIZE) + 2u))

/** 测试读写缓冲（按单块数据上限）。 */
static uint8 wrbuf[MINIFEE_MAX_BLOCK_SIZE];
static uint8 rdbuf[MINIFEE_MAX_BLOCK_SIZE];

/** 默认逐块 size 表（8 块 × 128B）。 */
static uint16 testSizes[TEST_NUM_BLOCKS] = {
    TEST_BLOCK_SIZE, TEST_BLOCK_SIZE, TEST_BLOCK_SIZE, TEST_BLOCK_SIZE,
    TEST_BLOCK_SIZE, TEST_BLOCK_SIZE, TEST_BLOCK_SIZE, TEST_BLOCK_SIZE
};

/**
 * \brief 以 seed+i 逐字节填充确定性测试模式（避开 0x55 起始，防止与提交页注入特征冲突）。
 * \param[out] buf 目标缓冲
 * \param[in] len 填充字节数
 * \param[in] seed 起始种子
 */
static void fill_pattern(uint8 *buf, uint16 len, uint8 seed)
{
    uint16 i;
    for (i = 0u; i < len; i++)
    {
        buf[i] = (uint8)(seed + (uint8)i);
    }
}

/** \brief TC-F-01：全 0xFF 首启，读取未写过块返回 NOT_FOUND；查询接口返回配置值。\req P0 #9、#12 */
static void tc_first_boot(void)
{
    FlashDrv_Stub_Reset();
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "Init first boot");
    CHECK(MiniFee_ReadBlock(0u, rdbuf, NULL_PTR) == MINIFEE_ERR_NOT_FOUND, "read unwritten block -> NOT_FOUND");
    CHECK(MiniFee_GetClusterCount() == MINIFEE_CLUSTER_NUM, "cluster count");
    CHECK(MiniFee_GetWritePageSize() == MINIFEE_PAGE_SIZE, "write page size");
    CHECK(MiniFee_GetBlockDataSize(0u) == TEST_BLOCK_SIZE, "block data size");
    CHECK(MiniFee_GetBlockDataSize(TEST_NUM_BLOCKS) == 0u, "invalid block -> size 0");
    CHECK(MiniFee_GetNumBlocks() == TEST_NUM_BLOCKS, "num blocks");
}

/** \brief TC-F-02：正常写读（写 → 读 → 比对，返回长度正确）。\req P0 #5、#10 */
static void tc_write_read_basic(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x10);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write block 0");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read block 0");
        CHECK(dl == 128u, "data len");
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "data matches");
    }
}

/**
 * \brief TC-F-02b：变长块槽写读（各块 size 不同，槽页数逐块不同）。
 * \req P0 #5、#10
 */
static void tc_variable_blocks(void)
{
    static uint16 varSizes[TEST_NUM_BLOCKS] = {16u, 64u, 240u, 32u, 128u, 16u, 96u, 64u};
    uint16 b;
    FlashDrv_Stub_Reset();
    CHECK(MiniFee_Init(varSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "Init var sizes");
    for (b = 0u; b < TEST_NUM_BLOCKS; b++)
    {
        uint16 dl = 0u;
        fill_pattern(wrbuf, varSizes[b], (uint8)(0x60u + (uint8)b));
        CHECK(MiniFee_WriteBlock(b, wrbuf, varSizes[b]) == MINIFEE_OK, "write var block");
        CHECK(MiniFee_ReadBlock(b, rdbuf, &dl) == MINIFEE_OK, "read var block");
        CHECK(dl == varSizes[b], "var data len");
        CHECK(memcmp(rdbuf, wrbuf, varSizes[b]) == 0, "var data matches");
    }
}

/**
 * \brief TC-F-03：块更新读回最新版本 v2（旧槽靠 seq 竞争 + GC 回收，无作废位）。
 * \req P0 #5、#6
 */
static void tc_block_update(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x20);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write v1");
    fill_pattern(wrbuf, 128u, 0x30);
    CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "write v2");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read after update");
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "latest value v2");
    }
    /* 重启后恢复仍取最新 seq */
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read after re-scan");
        fill_pattern(wrbuf, 128u, 0x30);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "v2 survives reboot");
    }
}

/**
 * \brief TC-F-04：写满 cluster 触发 GC + 磨损轮转。
 * \details Model A：所有写追加到 active cluster；槽放不下时 GC 把全部 live 槽搬到
 *          目标 cluster 并擦除原 cluster。block1/block2 各写一槽 + block0 反复写
 *          填满 active（1024 页）后再写触发 GC。
 * \req P0 #6、#7
 */
static void tc_gc_and_wear(void)
{
    uint16 i;
    /* 填写次数：使 block1+block2 两槽 + block0 填写后 active 仅剩不足一槽空间 */
    uint16 fills = (uint16)(((uint32)MINIFEE_PAGES_PER_CLUSTER - (3u * (uint32)TEST_SLOT_PAGES)) /
                            (uint32)TEST_SLOT_PAGES) + 1u;
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x11);
    CHECK(MiniFee_WriteBlock(1u, wrbuf, 128u) == MINIFEE_OK, "write block1");
    fill_pattern(wrbuf, 128u, 0x22);
    CHECK(MiniFee_WriteBlock(2u, wrbuf, 128u) == MINIFEE_OK, "write block2");
    /* block0 反复写产生垃圾并填满 active cluster */
    for (i = 0u; i < fills; i++)
    {
        fill_pattern(wrbuf, 128u, (uint8)(0x30u + (uint8)(i & 0x0Fu)));
        CHECK(MiniFee_WriteBlock(0u, wrbuf, 128u) == MINIFEE_OK, "fill block0");
    }
    /* 再写一次 block0：槽放不进 active，触发 GC（搬运 block1/block2/block0(latest) 到目标 cluster） */
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
    /* 重启后 GC 结果稳定 */
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan after GC");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(0u, rdbuf, &dl) == MINIFEE_OK, "read block0 after reboot");
        fill_pattern(wrbuf, 128u, 0x77);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "block0 latest survives reboot");
    }
}

/**
 * \brief TC-F-05：CRC 损坏识别（SetByte 注入数据损坏，重扫描后读报 ERR_CRC）。
 * \details 块 1 槽位于 cluster0 页 0 起，数据区首字节地址 = 槽 base + PAGE_SIZE。
 * \req P0 #10
 */
static void tc_crc_corrupt(void)
{
    uint16 dl = 0u;
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x40);
    CHECK(MiniFee_WriteBlock(1u, wrbuf, 128u) == MINIFEE_OK, "write block1");
    /* 直接破坏数据区首字节（绕过写约束） */
    FlashDrv_Stub_SetByte((uint32)MINIFEE_PAGE_SIZE,
                          (uint8)(FlashDrv_Stub_GetByte((uint32)MINIFEE_PAGE_SIZE) ^ 0xFFu));
    /* 重新扫描后读取应报告 CRC 错误 */
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan");
    CHECK(MiniFee_ReadBlock(1u, rdbuf, &dl) == MINIFEE_ERR_CRC, "CRC corruption detected");
}

/**
 * \brief TC-F-06：EraseBlock 写墓碑槽 + 幂等，擦后读返回 NOT_FOUND。
 * \req P0 #11
 */
static void tc_erase_block(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x50);
    CHECK(MiniFee_WriteBlock(2u, wrbuf, 128u) == MINIFEE_OK, "write block2");
    CHECK(MiniFee_EraseBlock(2u) == MINIFEE_OK, "erase block2 (tombstone)");
    CHECK(MiniFee_ReadBlock(2u, rdbuf, NULL_PTR) == MINIFEE_ERR_NOT_FOUND, "read erased -> NOT_FOUND");
    CHECK(MiniFee_EraseBlock(2u) == MINIFEE_OK, "erase idempotent");
    /* 重启后墓碑仍生效（擦除语义持久） */
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan after erase");
    CHECK(MiniFee_ReadBlock(2u, rdbuf, NULL_PTR) == MINIFEE_ERR_NOT_FOUND, "erased stays NOT_FOUND");
}

/**
 * \brief TC-F-07：掉电丢"最新一槽"——FailStatusWrites 在提交页写入步失败，恢复后回到上一版本。
 * \req P0 #9
 */
static void tc_power_loss_latest(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    fill_pattern(wrbuf, 128u, 0x01);
    CHECK(MiniFee_WriteBlock(3u, wrbuf, 128u) == MINIFEE_OK, "write v1");
    /* 让下一次提交页写失败，模拟掉电 */
    FlashDrv_Stub_FailStatusWrites(1u);
    fill_pattern(wrbuf, 128u, 0x02);
    CHECK(MiniFee_WriteBlock(3u, wrbuf, 128u) == MINIFEE_ERR_FLASH, "write v2 lost at commit");
    /* 重新扫描恢复：v2 半写槽被丢弃，应读回 v1 */
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "re-scan after power loss");
    {
        uint16 dl = 0u;
        CHECK(MiniFee_ReadBlock(3u, rdbuf, &dl) == MINIFEE_OK, "read after loss");
        fill_pattern(wrbuf, 128u, 0x01);
        CHECK(memcmp(rdbuf, wrbuf, 128u) == 0, "reverted to v1 (latest lost)");
    }
}

/** \brief TC-F-08：参数非法（越界/NULL/len 过大）返回 ERR_PARAM。 */
static void tc_param_errors(void)
{
    FlashDrv_Stub_Reset();
    (void)MiniFee_Init(testSizes, TEST_NUM_BLOCKS);
    CHECK(MiniFee_WriteBlock(TEST_NUM_BLOCKS, wrbuf, 1u) == MINIFEE_ERR_PARAM, "block oob");
    CHECK(MiniFee_WriteBlock(0u, NULL_PTR, 1u) == MINIFEE_ERR_PARAM, "null src");
    CHECK(MiniFee_WriteBlock(0u, wrbuf, (uint16)(TEST_BLOCK_SIZE + 1u)) == MINIFEE_ERR_PARAM, "len too big");
    CHECK(MiniFee_ReadBlock(0u, NULL_PTR, NULL_PTR) == MINIFEE_ERR_PARAM, "null dest");
}

/**
 * \brief TC-F-09：Init 参数校验（NULL size 表 / 块数 0 / 超上限 / size 非 PAGE_SIZE 整数倍）。
 * \details 容量约束 ΣblockPages+max ≤ pagesPerCluster 在当前配置下无法触发，
 *          实现中作防御性校验保留（docs/05 §2）。
 */
static void tc_init_param(void)
{
    static uint16 badSizes[1] = {9u}; /* 非 PAGE_SIZE 整数倍 */
    FlashDrv_Stub_Reset();
    CHECK(MiniFee_Init(NULL_PTR, TEST_NUM_BLOCKS) == MINIFEE_ERR_PARAM, "null sizes rejected");
    CHECK(MiniFee_Init(testSizes, 0u) == MINIFEE_ERR_PARAM, "numBlocks=0 rejected");
    CHECK(MiniFee_Init(testSizes, (uint16)(MINIFEE_MAX_NUM_BLOCKS + 1u)) == MINIFEE_ERR_PARAM, "numBlocks>MAX rejected");
    CHECK(MiniFee_Init(badSizes, 1u) == MINIFEE_ERR_PARAM, "size not multiple of PAGE_SIZE rejected");
    /* 全部失败后正常初始化仍可用 */
    CHECK(MiniFee_Init(testSizes, TEST_NUM_BLOCKS) == MINIFEE_OK, "valid Init after rejects");
}

/** \brief 依次运行 TC-F-01~TC-F-09。 */
void run_minifee_tests(void)
{
    tc_first_boot();
    tc_write_read_basic();
    tc_variable_blocks();
    tc_block_update();
    tc_gc_and_wear();
    tc_crc_corrupt();
    tc_erase_block();
    tc_power_loss_latest();
    tc_param_errors();
    tc_init_param();
}
