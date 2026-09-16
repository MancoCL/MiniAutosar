/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniNvm_Test.c
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniNvm 开发阶段功能自检实现（参数、空白、读写、多块、取消及轮换/迁移用例）。
 **
 **  @note               : 自检会擦除并重写数据 Flash 区域，禁止在量产流程中调用。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      用例适配新语义（空白返回全零、脏块落盘）
 **
 ***********************************************************************************************************************/

/* =================================================== inclusions =================================================== */
#include "MiniNvm_Test.h"
#include "MiniFee_Cfg.h"
#include "MiniFlsIf.h"
#include "MemIf_Types.h"

#define MININVM_TEST_POLL_GUARD       (2000000UL)
#define MININVM_TEST_LENGTH           (MININVM_MAX_BLOCK_LENGTH)
#define MININVM_TEST_ROT_ROUNDS       (8u)   /* 轮换/迁移用例轮数：每轮整块重写全部 Block，触发多次簇迁移 */

/* ================================================== module overview =============================================== */
/**
 * @brief           实现说明。
 * @details         本文件是 MiniNvm 的开发自检，通过直接调用 MiniNvm/MiniFee/MiniFlsIf 接口并显式轮询
 *                  三者的 MainFunction 来驱动完整异步链路，逐用例校验返回结果与数据一致性。
 *                  轮询均带 MININVM_TEST_POLL_GUARD 上限，超时判定失败，避免死循环。
 */

/* ============================================ external data definitions =========================================== */
MiniNvm_TestResultType MiniNvm_TestResult;

/* ============================================ internal data definitions =========================================== */
static uint8 MiniNvm_Test_Work[MININVM_TEST_LENGTH];      /**< 实际读回数据工作缓冲 */
static uint8 MiniNvm_Test_Expected[MININVM_TEST_LENGTH];  /**< 期望数据缓冲 */
static uint8 MiniNvm_Test_CurrentCase;                    /**< 当前用例编号 */

/**
 * @brief           测试本地字节拷贝
 * @param[out]      destination: 目标缓冲。
 * @param[in]       source: 源缓冲。
 * @param[in]       length: 拷贝字节数。
 * @return          void
 */
static void MiniNvm_Test_Copy(uint8* destination, const uint8* source, uint32 length)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        destination[index] = source[index];
    }
}

/**
 * @brief           用固定规律填充测试数据（seed 决定内容），便于重复构造可预期的数据
 * @param[out]      data: 待填充缓冲。
 * @param[in]       length: 填充字节数。
 * @param[in]       seed: 起始种子，逐字节递增填充。
 * @return          void
 */
static void MiniNvm_Test_Fill(uint8* data, uint32 length, uint8 seed)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        data[index] = (uint8)((index + (uint32)seed) & 0xFFu);
    }
}

/**
 * @brief           标记开始执行某个用例
 * @param[in]       caseId: 用例编号。
 * @return          void
 */
static void MiniNvm_Test_Begin(uint8 caseId)
{
    MiniNvm_Test_CurrentCase = caseId;
    MiniNvm_TestResult.CurCase = caseId;
}

/**
 * @brief           记录当前用例在某步骤失败并返回 E_NOT_OK
 * @param[in]       step: 失败步骤编号。
 * @return          uint8
 * @retval          E_NOT_OK: 固定返回失败。
 */
static uint8 MiniNvm_Test_Fail(uint8 step)
{
    MiniNvm_TestResult.FailCaseId = MiniNvm_Test_CurrentCase;
    MiniNvm_TestResult.FailStep = step;
    return E_NOT_OK;
}

/**
 * @brief           比较 expected 与 actual，不一致时记录首个差异字节位置并返回 E_NOT_OK
 * @param[in]       step: 检查点编号。
 * @param[in]       expected: 期望数据。
 * @param[in]       actual: 实际数据。
 * @param[in]       length: 比较字节数。
 * @return          uint8
 * @retval          E_OK: 完全一致。
 * @retval          E_NOT_OK: 存在差异，已记录首个不一致字节。
 */
static uint8 MiniNvm_Test_Check(uint8 step, const uint8* expected,
                                const uint8* actual, uint32 length)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        if (expected[index] != actual[index])
        {
            MiniNvm_TestResult.FailCaseId = MiniNvm_Test_CurrentCase;
            MiniNvm_TestResult.FailStep = step;
            MiniNvm_TestResult.FailOffset = (uint16)index;
            MiniNvm_TestResult.ExpectByte = expected[index];
            MiniNvm_TestResult.ActualByte = actual[index];
            return E_NOT_OK;
        }
    }
    return E_OK;
}

/**
 * @brief           擦除全部 Cluster：直接从 MiniFlsIf 发起擦除并阻塞轮询至 IDLE，构造空白起点
 * @return          uint8
 * @retval          E_OK: 全部 Cluster 擦除完成。
 * @retval          E_NOT_OK: 擦除失败或超时。
 */
static uint8 MiniNvm_Test_EraseAll(void)
{
    uint8 clusterIndex;

    for (clusterIndex = 0u; clusterIndex < MINIFEE_CLUSTER_COUNT; clusterIndex++)
    {
        uint32 guard = MININVM_TEST_POLL_GUARD;
        uint32 offset = MiniFee_ClusterConfig[clusterIndex].StartAddress - MINIFEE_FLS_BASE;

        if (MiniFlsIf_Erase(offset, MiniFee_ClusterConfig[clusterIndex].Length) != E_OK)
        {
            return E_NOT_OK;
        }
        while (MiniFlsIf_GetStatus() == MEMIF_BUSY)
        {
            if (guard-- == 0u)
            {
                return E_NOT_OK;
            }
            MiniFlsIf_MainFunction();
        }
        if (MiniFlsIf_GetStatus() != MEMIF_IDLE)
        {
            return E_NOT_OK;
        }
    }
    return E_OK;
}

/**
 * @brief           驱动单个 Block 的异步请求直至完成：按 FlsIf->Fee->Nvm 顺序轮询，返回该块最终结果
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @return          MiniNvm_RequestResultType: 该块最终结果。
 */
static MiniNvm_RequestResultType MiniNvm_Test_DriveBlock(uint8 blockId)
{
    uint32 guard = MININVM_TEST_POLL_GUARD;
    MiniNvm_RequestResultType result = MININVM_REQ_PENDING;

    while (result == MININVM_REQ_PENDING)
    {
        MiniFlsIf_MainFunction();
        MiniFee_MainFunction();
        MiniNvm_MainFunction();
        if (guard-- == 0u)
        {
            (void)MiniNvm_CancelJobs();
            return MININVM_REQ_NOT_OK;
        }
        if (MiniNvm_GetErrorStatus(blockId, &result) != E_OK)
        {
            return MININVM_REQ_NOT_OK;
        }
    }

    return result;
}

/**
 * @brief           驱动多块作业直至完成：按 FlsIf->Fee->Nvm 顺序轮询，返回总体结果
 * @return          MiniNvm_RequestResultType: 多块作业最终结果。
 */
static MiniNvm_RequestResultType MiniNvm_Test_DriveMulti(void)
{
    uint32 guard = MININVM_TEST_POLL_GUARD;
    MiniNvm_RequestResultType result = MININVM_REQ_PENDING;

    while (result == MININVM_REQ_PENDING)
    {
        MiniFlsIf_MainFunction();
        MiniFee_MainFunction();
        MiniNvm_MainFunction();
        if (guard-- == 0u)
        {
            (void)MiniNvm_CancelJobs();
            return MININVM_REQ_NOT_OK;
        }
        result = MiniNvm_GetMultiJobStatus();
    }

    return result;
}

/**
 * @brief           校验指定块的最终结果是否等于 expected
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @param[in]       expected: 期望结果。
 * @return          uint8
 * @retval          E_OK: 与期望一致。
 * @retval          E_NOT_OK: 不一致。
 */
static uint8 MiniNvm_Test_GetResult(uint8 blockId, MiniNvm_RequestResultType expected)
{
    MiniNvm_RequestResultType result;

    if (MiniNvm_GetErrorStatus(blockId, &result) != E_OK)
    {
        return E_NOT_OK;
    }
    return (result == expected) ? E_OK : E_NOT_OK;
}

/**
 * @brief           用例：非法参数与未初始化请求应被拒绝（返回 E_NOT_OK）
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseParam(void)
{
    uint8 data = 0u;

    MiniNvm_Test_Begin(MININVM_TEST_CASE_PARAM);
    if (MiniNvm_ReadBlock(0u, &data) != E_NOT_OK)
    {
        return MiniNvm_Test_Fail(1u);
    }
    if (MiniNvm_ReadBlock(1u, NULL_PTR) != E_NOT_OK)
    {
        return MiniNvm_Test_Fail(2u);
    }
    if (MiniNvm_WriteBlock(1u, NULL_PTR) != E_NOT_OK)
    {
        return MiniNvm_Test_Fail(3u);
    }
    if (MiniNvm_ReadBlock((uint8)(MININVM_BLOCK_COUNT + 1u), &data) != E_NOT_OK)
    {
        return MiniNvm_Test_Fail(4u);
    }
    if (MiniNvm_CancelJobs() != E_OK)
    {
        return MiniNvm_Test_Fail(5u);
    }
    return E_OK;
}

/**
 * @brief           用例：空白 Flash 中读取无记录块应返回全零默认值并成功，且多块结果保持 IDLE
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseBlank(void)
{
    uint32 index;
    uint32 length = MiniNvm_BlockDescriptor[0].Length;
    MiniNvm_RequestResultType result;

    MiniNvm_Test_Begin(MININVM_TEST_CASE_BLANK);
    if (MiniNvm_ReadBlock(1u, MiniNvm_Test_Work) != E_OK)
    {
        return MiniNvm_Test_Fail(1u);
    }
    (void)MiniNvm_Test_DriveBlock(1u);
    if (MiniNvm_Test_GetResult(1u, MININVM_REQ_OK) != E_OK)
    {
        return MiniNvm_Test_Fail(2u);
    }
    /* 无记录时按默认值全零返回 */
    for (index = 0u; index < length; index++)
    {
        if (MiniNvm_Test_Work[index] != 0u)
        {
            return MiniNvm_Test_Fail(3u);
        }
    }
    result = MiniNvm_GetMultiJobStatus();
    return (result == MININVM_REQ_IDLE) ? E_OK : MiniNvm_Test_Fail(4u);
}

/**
 * @brief           用例：单块写入后读回，数据应完全一致
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseBasic(void)
{
    MiniNvm_RequestResultType result;
    uint32 length = MiniNvm_BlockDescriptor[0].Length;

    MiniNvm_Test_Begin(MININVM_TEST_CASE_BASIC);
    MiniNvm_Test_Fill(MiniNvm_Test_Expected, length, 0x21u);
    if (MiniNvm_WriteBlock(1u, MiniNvm_Test_Expected) != E_OK)
    {
        return MiniNvm_Test_Fail(1u);
    }
    result = MiniNvm_Test_DriveBlock(1u);
    if ((result != MININVM_REQ_OK) ||
        (MiniNvm_Test_GetResult(1u, MININVM_REQ_OK) != E_OK))
    {
        return MiniNvm_Test_Fail(2u);
    }
    MiniNvm_Test_Copy(MiniNvm_Test_Work, MiniNvm_Test_Expected, length);
    if (MiniNvm_ReadBlock(1u, MiniNvm_Test_Work) != E_OK)
    {
        return MiniNvm_Test_Fail(3u);
    }
    (void)MiniNvm_Test_DriveBlock(1u);
    if (MiniNvm_Test_GetResult(1u, MININVM_REQ_OK) != E_OK)
    {
        return MiniNvm_Test_Fail(4u);
    }
    return MiniNvm_Test_Check(5u, MiniNvm_Test_Expected, MiniNvm_Test_Work, length);
}

/**
 * @brief           用例：写入请求入队后立即改写源缓冲，验证入队快照生效、最终仍写入原数据
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseSourceCopy(void)
{
    uint32 length = MiniNvm_BlockDescriptor[1].Length;
    uint8 source[MININVM_MAX_BLOCK_LENGTH];

    MiniNvm_Test_Begin(MININVM_TEST_CASE_SOURCE_COPY);
    MiniNvm_Test_Fill(source, length, 0x41u);
    MiniNvm_Test_Copy(MiniNvm_Test_Expected, source, length);
    if (MiniNvm_WriteBlock(2u, source) != E_OK)
    {
        return MiniNvm_Test_Fail(1u);
    }
    MiniNvm_Test_Fill(source, length, 0xA1u);
    (void)MiniNvm_Test_DriveBlock(2u);
    if (MiniNvm_Test_GetResult(2u, MININVM_REQ_OK) != E_OK)
    {
        return MiniNvm_Test_Fail(2u);
    }
    MiniNvm_Test_Copy(MiniNvm_Test_Work, source, length);
    if (MiniNvm_ReadBlock(2u, MiniNvm_Test_Work) != E_OK)
    {
        return MiniNvm_Test_Fail(3u);
    }
    (void)MiniNvm_Test_DriveBlock(2u);
    if (MiniNvm_Test_GetResult(2u, MININVM_REQ_OK) != E_OK)
    {
        return MiniNvm_Test_Fail(4u);
    }
    return MiniNvm_Test_Check(5u, MiniNvm_Test_Expected, MiniNvm_Test_Work, length);
}

/**
 * @brief           用例：经 WriteRam 置脏后 WriteAll 落盘、ReadAll 读回，逐块校验数据一致
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseAll(void)
{
    uint8 index;
    const MiniNvm_BlockDescriptorType* descriptor;

    MiniNvm_Test_Begin(MININVM_TEST_CASE_ALL);
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        descriptor = &MiniNvm_BlockDescriptor[index];
        /* 经 WriteRam 写入以置脏（WriteAll 仅落盘脏块） */
        MiniNvm_Test_Fill(MiniNvm_Test_Expected,
                          descriptor->Length,
                          (uint8)(0x70u + index));
        (void)MiniNvm_WriteRam(descriptor->MiniFeeBlockId,
                               MiniNvm_Test_Expected,
                               descriptor->Length);
    }

    if (MiniNvm_WriteAll() != E_OK)
    {
        return MiniNvm_Test_Fail(1u);
    }
    (void)MiniNvm_Test_DriveMulti();
    if (MiniNvm_GetMultiJobStatus() != MININVM_REQ_OK)
    {
        return MiniNvm_Test_Fail(2u);
    }
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        descriptor = &MiniNvm_BlockDescriptor[index];
        MiniNvm_Test_Fill(descriptor->RamBlockAddress,
                          descriptor->Length,
                          0u);
    }

    if (MiniNvm_ReadAll() != E_OK)
    {
        return MiniNvm_Test_Fail(3u);
    }
    (void)MiniNvm_Test_DriveMulti();
    if (MiniNvm_GetMultiJobStatus() != MININVM_REQ_OK)
    {
        return MiniNvm_Test_Fail(4u);
    }

    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        descriptor = &MiniNvm_BlockDescriptor[index];
        MiniNvm_Test_Fill(MiniNvm_Test_Expected,
                          descriptor->Length,
                          (uint8)(0x70u + index));
        if (MiniNvm_Test_Check(5u,
                               MiniNvm_Test_Expected,
                               descriptor->RamBlockAddress,
                               descriptor->Length) != E_OK)
        {
            return E_NOT_OK;
        }
    }

    return E_OK;
}

/**
 * @brief           用例：排入两个写请求后取消作业，两块结果都应置 NOT_OK
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseCancel(void)
{
    MiniNvm_Test_Begin(MININVM_TEST_CASE_CANCEL);
    MiniNvm_Test_Fill(MiniNvm_Test_Expected, MiniNvm_BlockDescriptor[0].Length, 0x61u);
    if (MiniNvm_WriteBlock(1u, MiniNvm_Test_Expected) != E_OK)
    {
        return MiniNvm_Test_Fail(1u);
    }
    if (MiniNvm_WriteBlock(2u, MiniNvm_Test_Expected) != E_OK)
    {
        return MiniNvm_Test_Fail(2u);
    }
    if (MiniNvm_CancelJobs() != E_OK)
    {
        return MiniNvm_Test_Fail(3u);
    }
    if (MiniNvm_Test_GetResult(1u, MININVM_REQ_NOT_OK) != E_OK)
    {
        return MiniNvm_Test_Fail(4u);
    }
    return MiniNvm_Test_GetResult(2u, MININVM_REQ_NOT_OK);
}

/**
 * @brief           经 MiniFlsIf 同步读取指定 Cluster 头（阻塞轮询，仅测试用）
 * @param[in]       clusterIndex: 簇下标。
 * @param[out]      hdrBuf: 目标缓冲，长度须 >= MINIFEE_CLUSTER_HEADER_SIZE。
 * @return          uint8
 * @retval          E_OK: 读取完成。
 * @retval          E_NOT_OK: 读取失败。
 */
static uint8 MiniNvm_Test_ReadClusterHeader(uint8 clusterIndex, uint8* hdrBuf)
{
    uint32 guard = MININVM_TEST_POLL_GUARD;
    uint32 offset = MiniFee_ClusterConfig[clusterIndex].StartAddress - MINIFEE_FLS_BASE;

    if (MiniFlsIf_Read(offset, hdrBuf, MINIFEE_CLUSTER_HEADER_SIZE) != E_OK)
    {
        return E_NOT_OK;
    }
    while (MiniFlsIf_GetStatus() == MEMIF_BUSY)
    {
        if (guard-- == 0u)
        {
            return E_NOT_OK;
        }
        MiniFlsIf_MainFunction();
    }
    if (MiniFlsIf_GetStatus() != MEMIF_IDLE)
    {
        return E_NOT_OK;
    }
    return E_OK;
}

/**
 * @brief           CRC-8（多项式 0x07，与 MiniFee 簇头校验一致），测试本地实现
 * @param[in]       data: 待计算数据。
 * @param[in]       length: 数据字节数。
 * @return          uint8: CRC-8 结果。
 */
static uint8 MiniNvm_Test_Crc8(const uint8* data, uint32 length)
{
    uint8 crc = 0u;
    uint32 i;
    uint8  j;

    for (i = 0u; i < length; i++)
    {
        crc ^= data[i];
        for (j = 0u; j < 8u; j++)
        {
            if ((crc & 0x80u) != 0u)
            {
                crc = (uint8)((crc << 1) ^ 0x07u);
            }
            else
            {
                crc = (uint8)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief           多次整块重写（内容每次变化）直至触发簇迁移/翻页，验证迁移后所有 Block 数据保全与活动簇 Generation 递增
 * @return          uint8: E_OK 通过 / E_NOT_OK 失败。
 */
static uint8 MiniNvm_Test_CaseRotate(void)
{
    uint8 r;
    uint8 index;
    const MiniNvm_BlockDescriptorType* descriptor;

    MiniNvm_Test_Begin(MININVM_TEST_CASE_ROTATE);

    for (r = 0u; r < MININVM_TEST_ROT_ROUNDS; r++)
    {
        for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
        {
            descriptor = &MiniNvm_BlockDescriptor[index];
            /* 经 WriteRam 写入以置脏（WriteAll 仅落盘脏块） */
            MiniNvm_Test_Fill(MiniNvm_Test_Expected, descriptor->Length,
                              (uint8)(0x90u + r + index));
            (void)MiniNvm_WriteRam(descriptor->MiniFeeBlockId,
                                   MiniNvm_Test_Expected,
                                   descriptor->Length);
        }
        if (MiniNvm_WriteAll() != E_OK)
        {
            return MiniNvm_Test_Fail(1u);
        }
        (void)MiniNvm_Test_DriveMulti();
        if (MiniNvm_GetMultiJobStatus() != MININVM_REQ_OK)
        {
            return MiniNvm_Test_Fail(2u);
        }

        /* 周期性 ReadAll 校验（覆盖迁移后的数据保全） */
        if ((r % 2u) == 1u)
        {
            for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
            {
                descriptor = &MiniNvm_BlockDescriptor[index];
                MiniNvm_Test_Fill(descriptor->RamBlockAddress, descriptor->Length, 0u);
            }
            if (MiniNvm_ReadAll() != E_OK)
            {
                return MiniNvm_Test_Fail(3u);
            }
            (void)MiniNvm_Test_DriveMulti();
            if (MiniNvm_GetMultiJobStatus() != MININVM_REQ_OK)
            {
                return MiniNvm_Test_Fail(4u);
            }
            for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
            {
                descriptor = &MiniNvm_BlockDescriptor[index];
                MiniNvm_Test_Fill(MiniNvm_Test_Expected, descriptor->Length,
                                  (uint8)(0x90u + r + index));
                if (MiniNvm_Test_Check(5u, MiniNvm_Test_Expected,
                                       descriptor->RamBlockAddress, descriptor->Length) != E_OK)
                {
                    return E_NOT_OK;
                }
            }
        }
    }

    /* 簇头校验：至少一个 ACTIVE 簇且 Generation ≥ 2（证明发生过簇迁移/翻页） */
    {
        uint8 hdrBuf[MINIFEE_CLUSTER_HEADER_SIZE];
        uint8 clusterIndex;
        uint8 foundActive = 0u;
        uint32 bestGen = 0u;

        for (clusterIndex = 0u; clusterIndex < MINIFEE_CLUSTER_COUNT; clusterIndex++)
        {
            uint32 magic;
            uint32 gen;

            if (MiniNvm_Test_ReadClusterHeader(clusterIndex, hdrBuf) != E_OK)
            {
                return MiniNvm_Test_Fail(6u);
            }
            magic = ((uint32)hdrBuf[0] << 24) | ((uint32)hdrBuf[1] << 16)
                    | ((uint32)hdrBuf[2] << 8)  | (uint32)hdrBuf[3];
            gen   = ((uint32)hdrBuf[4] << 16) | ((uint32)hdrBuf[5] << 8) | (uint32)hdrBuf[6];
            /* 簇头须通过 CRC-8 校验（适配 MiniFee 新簇头：Magic 4B + Generation 3B，无 Status） */
            if ((hdrBuf[7] == MiniNvm_Test_Crc8(hdrBuf, 7u)) &&
                (magic == MINIFEE_CLUSTER_MAGIC))
            {
                if (gen > bestGen)
                {
                    bestGen = gen;
                }
                foundActive = 1u;
            }
        }
        if ((foundActive == 0u) || (bestGen < 2u))
        {
            return MiniNvm_Test_Fail(7u);
        }
    }

    return E_OK;
}

/**
 * @brief           统计单个用例通过/失败
 * @param[in]       result: 用例结果（E_OK 计通过，否则计失败）。
 * @return          void
 */
static void MiniNvm_Test_Record(uint8 result)
{
    if (result == E_OK)
    {
        MiniNvm_TestResult.PassCount++;
    }
    else
    {
        MiniNvm_TestResult.FailCount++;
    }
}

/**
 * @brief           自检入口：先验证未初始化拒绝请求，再初始化并擦除全簇，
 * 依次执行全部用例，最后汇总 OverallResult（FailCount==0 则 E_OK）。
 * @return          uint8
 * @retval          E_OK: 全部用例通过。
 * @retval          E_NOT_OK: 存在失败用例，详情见 MiniNvm_TestResult。
 */
uint8 MiniNvm_Test(void)
{
    uint8 result;

    MiniNvm_TestResult.OverallResult = E_NOT_OK;
    MiniNvm_TestResult.CurCase = 0u;
    MiniNvm_TestResult.FailCaseId = 0u;
    MiniNvm_TestResult.FailStep = 0u;
    MiniNvm_TestResult.FailOffset = 0u;
    MiniNvm_TestResult.ExpectByte = 0u;
    MiniNvm_TestResult.ActualByte = 0u;
    MiniNvm_TestResult.PassCount = 0u;
    MiniNvm_TestResult.FailCount = 0u;
    MiniNvm_Test_CurrentCase = 0u;

    /* 复位模块为未初始化状态，保证下述“未初始化拒绝请求”检查不依赖外部环境的初始化状态 */
    MiniNvm_DeInit();

    if (MiniNvm_ReadBlock(1u, MiniNvm_Test_Work) != E_NOT_OK)
    {
        MiniNvm_TestResult.FailCaseId = MININVM_TEST_CASE_PARAM;
        MiniNvm_TestResult.FailStep = 0u;
        MiniNvm_TestResult.FailCount = 1u;
        return E_NOT_OK;
    }

    MiniNvm_Init();
    result = MiniNvm_Test_EraseAll();
    if (result != E_OK)
    {
        MiniNvm_TestResult.FailCaseId = MININVM_TEST_CASE_BLANK;
        MiniNvm_TestResult.FailStep = 0u;
        MiniNvm_TestResult.FailCount = 1u;
        return E_NOT_OK;
    }

    MiniNvm_Test_Record(MiniNvm_Test_CaseParam());
    MiniNvm_Test_Record(MiniNvm_Test_CaseBlank());
    MiniNvm_Test_Record(MiniNvm_Test_CaseBasic());
    MiniNvm_Test_Record(MiniNvm_Test_CaseSourceCopy());
    MiniNvm_Test_Record(MiniNvm_Test_CaseAll());
    MiniNvm_Test_Record(MiniNvm_Test_CaseCancel());
    MiniNvm_Test_Record(MiniNvm_Test_CaseRotate());

    MiniNvm_TestResult.OverallResult =
        (MiniNvm_TestResult.FailCount == 0u) ? E_OK : E_NOT_OK;
    return MiniNvm_TestResult.OverallResult;
}