/**
 * \file MiniNvm.c
 * \brief MiniNvm（NVRAM 管理）实现
 *
 * 设计要点（详见 docs/03_mininvm_design.md）：
 *  - 块类型：仅 Native 单副本；块 ID、数量、大小和 RAM mirror 来自 MiniNvm_Cfg.h。
 *  - RAM 镜像策略（P0 第 13 条）：
 *      * Init：接收集成方块配置表+RAM缓冲，按 size 累加计算 ramOffset，镜像填缺省，状态置 UNINIT。
 *      * ReadAll（上电）：逐块经 MiniFee_ReadBlock 读入镜像，CRC 校验，无效块装缺省并置 INVALID。
 *      * WriteBlock（运行期）：只更新镜像 + dirty，不落 Flash（同步）。
 *      * EraseNvBlock：镜像置缺省 + dirty + INVALID。
 *      * WriteAll（跳转 APP/下电前）：逐 dirty 块经 MiniFee 回写，VALID→WriteBlock，INVALID→EraseBlock。
 *  - 访问链路：bootloader → MiniNvm → MiniFee → FlashDrv，绝不绕过。
 *  - 全同步、纯 C、无动态内存、OS 无关。
 */

#include "MiniNvm.h"
#include "MiniNvm_Cfg.h"
#include "MiniFee.h"
#include <string.h>

/* ---- 编译期配置提供的块表与 RAM mirror ---- */
static const MiniNvm_BlockConfigType *blockTable = MiniNvm_BlockConfig;
static uint16 numBlocks = (uint16)MININVM_MAX_NUM_BLOCKS;
static uint8 *ramMirror = MiniNvm_RamMirror;
static uint16 ramOffsets[MININVM_MAX_NUM_BLOCKS]; /* Init 时按 size 累加计算 */

/* ---- 运行期状态（静态数组，按上限声明） ---- */
static boolean                  dirty[MININVM_MAX_NUM_BLOCKS];
static MiniNvm_BlockStateType   blockState[MININVM_MAX_NUM_BLOCKS];
static MiniNvm_ErrorStatusType  blockErr[MININVM_MAX_NUM_BLOCKS];
static boolean inited = FALSE;

/* ---- 内部助手 ---- */
static void fillDefault(uint8 *buf, uint16 size)
{
    uint16 i;
    for (i = 0u; i < size; i++)
    {
        buf[i] = MININVM_DEFAULT_BYTE;
    }
}

static boolean validBlockId(uint16 blockId)
{
    return (blockId < numBlocks) ? TRUE : FALSE;
}

/* ---- 对外 API ---- */

Std_ReturnType MiniNvm_Init(void)
{
    uint16 b;
    uint16 offset = 0u;

    /* 先初始化 MiniFee（含 Flash 扫描恢复） */
    if (MiniFee_Init(numBlocks) != MINIFEE_OK)
    {
        inited = FALSE;
        return E_NOT_OK;
    }

    /* 逐块累加 size 计算 RAM 偏移，并初始化镜像/状态 */
    for (b = 0u; b < numBlocks; b++)
    {
        ramOffsets[b] = offset;
        fillDefault(&ramMirror[offset], blockTable[b].size);
        offset += blockTable[b].size;
        dirty[b] = FALSE;
        blockState[b] = MININVM_BLOCK_UNINIT;
        blockErr[b] = MININVM_ERR_NONE;
    }
    /* 安全校验：RAM 缓冲须覆盖所有块 size 之和 */
    if (offset > MININVM_RAM_MIRROR_SIZE)
    {
        inited = FALSE;
        return E_NOT_OK;
    }
    inited = TRUE;
    return E_OK;
}

Std_ReturnType MiniNvm_ReadAll(void)
{
    uint16 b;
    if (!inited)
    {
        return E_NOT_OK;
    }

    for (b = 0u; b < numBlocks; b++)
    {
        if (blockTable[b].readAll == FALSE)
        {
            continue;
        }
        {
            uint8 *mirror = &ramMirror[ramOffsets[b]];
            uint16 len = 0u;
            MiniFee_ReturnType r = MiniFee_ReadBlock((uint16)b, mirror, &len);
            switch (r)
            {
                case MINIFEE_OK:
                    /* 数据区可能小于块大小，剩余保持缺省 */
                    if (len < blockTable[b].size)
                    {
                        fillDefault(&mirror[len], (uint16)(blockTable[b].size - len));
                    }
                    blockState[b] = MININVM_BLOCK_VALID;
                    blockErr[b] = MININVM_ERR_NONE;
                    break;
                case MINIFEE_ERR_NOT_FOUND:
                    /* 首启/未写过：装缺省，置 INVALID（非致命） */
                    fillDefault(mirror, blockTable[b].size);
                    blockState[b] = MININVM_BLOCK_INVALID;
                    blockErr[b] = MININVM_ERR_NONE;
                    break;
                case MINIFEE_ERR_CRC:
                    fillDefault(mirror, blockTable[b].size);
                    blockState[b] = MININVM_BLOCK_INVALID;
                    blockErr[b] = MININVM_ERR_CRC;
                    break;
                default:
                    /* FLASH/其它错误：装缺省，置 INVALID，记读错误 */
                    fillDefault(mirror, blockTable[b].size);
                    blockState[b] = MININVM_BLOCK_INVALID;
                    blockErr[b] = MININVM_ERR_READ;
                    break;
            }
        }
    }
    return E_OK;
}

Std_ReturnType MiniNvm_ReadBlock(uint16 blockId, uint8 *dataBuf)
{
    if (!inited)
    {
        return E_NOT_OK;
    }
    if ((!validBlockId(blockId)) || (dataBuf == NULL_PTR))
    {
        return E_NOT_OK;
    }
    if (blockState[blockId] == MININVM_BLOCK_UNINIT)
    {
        return E_NOT_OK; /* 未 ReadAll */
    }
    (void)memcpy(dataBuf, &ramMirror[ramOffsets[blockId]], blockTable[blockId].size);
    return (blockState[blockId] == MININVM_BLOCK_VALID) ? E_OK : E_NOT_OK;
}

Std_ReturnType MiniNvm_WriteBlock(uint16 blockId, const uint8 *dataBuf)
{
    if (!inited)
    {
        return E_NOT_OK;
    }
    if ((!validBlockId(blockId)) || (dataBuf == NULL_PTR))
    {
        return E_NOT_OK;
    }
    (void)memcpy(&ramMirror[ramOffsets[blockId]], dataBuf, blockTable[blockId].size);
    dirty[blockId] = TRUE;
    blockState[blockId] = MININVM_BLOCK_VALID;
    blockErr[blockId] = MININVM_ERR_NONE;
    return E_OK;
}

Std_ReturnType MiniNvm_EraseNvBlock(uint16 blockId)
{
    if (!inited)
    {
        return E_NOT_OK;
    }
    if (!validBlockId(blockId))
    {
        return E_NOT_OK;
    }
    fillDefault(&ramMirror[ramOffsets[blockId]], blockTable[blockId].size);
    dirty[blockId] = TRUE;
    blockState[blockId] = MININVM_BLOCK_INVALID;
    blockErr[blockId] = MININVM_ERR_NONE;
    return E_OK;
}

Std_ReturnType MiniNvm_WriteAll(void)
{
    uint16 b;
    boolean allOk = TRUE;

    if (!inited)
    {
        return E_NOT_OK;
    }

    for (b = 0u; b < numBlocks; b++)
    {
        MiniFee_ReturnType r;
        if (blockTable[b].writeAll == FALSE)
        {
            continue;
        }
        if (!dirty[b])
        {
            continue;
        }
        if (blockState[b] == MININVM_BLOCK_VALID)
        {
            r = MiniFee_WriteBlock((uint16)b, &ramMirror[ramOffsets[b]], blockTable[b].size);
            if (r != MINIFEE_OK)
            {
                blockErr[b] = MININVM_ERR_WRITE;
                allOk = FALSE;
                continue; /* 保持 dirty，待下次重试 */
            }
        }
        else /* INVALID：作废 Flash 页 */
        {
            r = MiniFee_EraseBlock((uint16)b);
            if (r != MINIFEE_OK)
            {
                blockErr[b] = MININVM_ERR_ERASE;
                allOk = FALSE;
                continue;
            }
        }
        dirty[b] = FALSE;
        blockErr[b] = MININVM_ERR_NONE;
    }
    return allOk ? E_OK : E_NOT_OK;
}

Std_ReturnType MiniNvm_GetErrorStatus(uint16 blockId, MiniNvm_ErrorStatusType *errorStatus)
{
    if (!inited)
    {
        return E_NOT_OK;
    }
    if ((!validBlockId(blockId)) || (errorStatus == NULL_PTR))
    {
        return E_NOT_OK;
    }
    if (blockState[blockId] == MININVM_BLOCK_UNINIT)
    {
        *errorStatus = (MiniNvm_ErrorStatusType)(blockErr[blockId] | MININVM_ERR_UNINIT);
    }
    else
    {
        *errorStatus = blockErr[blockId];
    }
    return E_OK;
}

uint16 MiniNvm_GetBlockSize(uint16 blockId)
{
    if (!validBlockId(blockId))
    {
        return 0u;
    }
    return blockTable[blockId].size;
}

uint16 MiniNvm_GetNumBlocks(void)
{
    return numBlocks;
}
