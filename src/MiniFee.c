/**
 * \file MiniFee.c
 * \brief MiniFee（Flash EEPROM 模拟）实现
 *
 * 设计要点（详见 docs/02_minifee_design.md）：
 *  - Flash 布局：clusterCount 个 cluster，每 cluster pagesPerCluster 个页。
 *  - 页结构：[页头10B][数据区 PAGE_DATA_SIZE][页尾6B: dataCrc(4)+status(2)]。
 *  - 状态机（Flash 1→0 友好）：ERASED(0xFFFF) → VALID(0x5555,提交) → INVALID(0x0000,作废)。
 *    提交位最后写；未提交页在掉电恢复时判为 DIRTY/INVALID，丢弃 → 满足"可丢最新一页"。
 *  - 磨损均衡：所有写都落到当前 active cluster；写满触发 GC，把有效页搬到目标 cluster，
 *    擦除原 cluster，目标 cluster 成为新 active。cluster 轮转 0→1→…→N-1→0。
 *  - 掉电恢复：启动扫描全部页，按 Magic/Status(VALID)/CRC 重建 blockId→最新页映射
 *    （同块取最大 seq），active 取"全局最大 seq 页"所在 cluster，writeCursor 取 active 内首个空页。
 *
 * 全同步、纯 C、无动态内存、OS 无关。
 */

#include "MiniFee.h"
#include "MiniFee_Cfg.h"
#include "FlashDrv.h"
#include <string.h>

/* ---- 页内字段偏移（派生自布局） ---- */
#define HDR_SIZE        10u
#define OFF_MAGIC       0u
#define OFF_BLOCKID     4u
#define OFF_SEQ         6u
#define OFF_DATALEN     8u
#define OFF_DATA        HDR_SIZE
#define DATA_SIZE       MINIFEE_PAGE_DATA_SIZE
#define OFF_DATACRC     (OFF_DATA + DATA_SIZE)   /* = PAGE_SIZE - 6 */
#define OFF_STATUS      (OFF_DATACRC + 4u)        /* = PAGE_SIZE - 2 */
#define FOOT_SIZE       6u

/* ---- 内部映射表条目 ---- */
typedef struct
{
    boolean valid;
    boolean corrupt;   /* 扫描时发现该块存在 VALID 状态但 CRC 损坏的页 */
    uint32  pageAddr;   /* 该块当前有效页的扁平地址 */
    uint16  seq;
} BlockMapEntry;

/* ---- 模块静态状态 ---- */
static FlashDrv_PropertyType flashProp;
static uint8  clusterNum;
static uint16 pagesPerCluster;
static uint16 numBlocks;
static uint8  activeCluster;
static uint16 writeCursor;
static boolean inited = FALSE;
static BlockMapEntry blockMap[MINIFEE_MAX_NUM_BLOCKS];
static uint8 pageBuf[MINIFEE_PAGE_SIZE];

/* ---- 字节序助手（小端） ---- */
static void putU16(uint8 *p, uint16 v)
{
    p[0] = (uint8)(v & 0xFFu);
    p[1] = (uint8)((v >> 8) & 0xFFu);
}
static void putU32(uint8 *p, uint32 v)
{
    p[0] = (uint8)(v & 0xFFu);
    p[1] = (uint8)((v >> 8) & 0xFFu);
    p[2] = (uint8)((v >> 16) & 0xFFu);
    p[3] = (uint8)((v >> 24) & 0xFFu);
}
static uint16 getU16(const uint8 *p)
{
    return (uint16)((uint16)p[0] | ((uint16)p[1] << 8));
}
static uint32 getU32(const uint8 *p)
{
    return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}
static boolean magicOk(const uint8 *buf)
{
    return (buf[0] == MINIFEE_MAGIC0) && (buf[1] == MINIFEE_MAGIC1) &&
           (buf[2] == MINIFEE_MAGIC2) && (buf[3] == MINIFEE_MAGIC3);
}

/* ---- 地址计算 ---- */
static uint32 clusterBase(uint8 c)
{
    return (uint32)c * flashProp.clusterSize;
}
static uint32 pageAddr(uint8 c, uint16 p)
{
    return clusterBase(c) + (uint32)p * flashProp.pageSize;
}

/* ---- CRC ---- */
static uint32 MiniFee_CalcCrc(const uint8 *data, uint32 len)
{
#if (MINIFEE_CRC_TYPE == MINIFEE_CRC_TYPE_CRC32)
    uint32 crc = MINIFEE_CRC32_INIT;
    uint32 i;
    uint8 b;
    for (i = 0u; i < len; i++)
    {
        crc ^= (uint32)data[i];
        for (b = 0u; b < 8u; b++)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1) ^ MINIFEE_CRC32_POLY;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc ^ MINIFEE_CRC32_XOROUT;
#else
    uint16 crc = MINIFEE_CRC16_INIT;
    uint32 i;
    uint8 b;
    for (i = 0u; i < len; i++)
    {
        crc ^= (uint16)data[i];
        for (b = 0u; b < 8u; b++)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (uint16)((crc >> 1) ^ MINIFEE_CRC16_POLY);
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return (uint32)(uint16)(crc ^ MINIFEE_CRC16_XOROUT);
#endif
}

/* ---- seq 新旧判定（带回绕） ---- */
static boolean isNewer(uint16 a, uint16 b)
{
    return (((int16)(a - b)) > 0) ? TRUE : FALSE;
}

/* ---- 单页写入：页头+数据 → dataCrc → 提交 VALID ---- */
static MiniFee_ReturnType writePage(uint32 addr, uint16 blockId, uint16 seq,
                                    uint16 dataLen, const uint8 *data)
{
    uint8 crcBuf[4];
    uint8 stBuf[2];
    uint32 crc;
    FlashDrv_ReturnType fr;
    uint16 i;

    /* 构建页头+数据到 pageBuf（页尾区域保持未定义，不影响） */
    pageBuf[0] = MINIFEE_MAGIC0;
    pageBuf[1] = MINIFEE_MAGIC1;
    pageBuf[2] = MINIFEE_MAGIC2;
    pageBuf[3] = MINIFEE_MAGIC3;
    putU16(&pageBuf[OFF_BLOCKID], blockId);
    putU16(&pageBuf[OFF_SEQ], seq);
    putU16(&pageBuf[OFF_DATALEN], dataLen);
    for (i = 0u; i < dataLen; i++)
    {
        pageBuf[OFF_DATA + i] = data[i];
    }
    /* CRC 覆盖 [0, OFF_DATA+dataLen) */
    crc = MiniFee_CalcCrc(pageBuf, (uint32)(OFF_DATA + dataLen));
    putU32(crcBuf, crc);

    /* 1) 写页头+数据 */
    fr = FlashDrv_Write(addr, (uint16)(OFF_DATA + dataLen), pageBuf);
    if (fr != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    /* 2) 写 dataCrc */
    fr = FlashDrv_Write(addr + OFF_DATACRC, 4u, crcBuf);
    if (fr != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    /* 3) 提交 status = VALID */
    putU16(stBuf, MINIFEE_STATUS_VALID);
    fr = FlashDrv_Write(addr + OFF_STATUS, 2u, stBuf);
    if (fr != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    return MINIFEE_OK;
}

/* ---- 作废页：status ← INVALID ---- */
static MiniFee_ReturnType invalidatePage(uint32 addr)
{
    uint8 stBuf[2];
    putU16(stBuf, MINIFEE_STATUS_INVALID);
    return (FlashDrv_Write(addr + OFF_STATUS, 2u, stBuf) == FLASH_OK) ? MINIFEE_OK : MINIFEE_ERR_FLASH;
}

/* ---- cluster 是否全空（无 magic） ---- */
static boolean clusterFullyErased(uint8 c)
{
    uint16 p;
    for (p = 0u; p < pagesPerCluster; p++)
    {
        uint8 hdr[4];
        if (FlashDrv_Read(pageAddr(c, p), 4u, hdr) != FLASH_OK)
        {
            return FALSE;
        }
        if (magicOk(hdr))
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* ---- GC：把 active 的有效页搬到目标 cluster，擦除 active，切换 ---- */
static MiniFee_ReturnType gcAndSwitch(void)
{
    uint8 target = 0xFFu;
    uint8 c;
    uint16 p;
    uint16 tgtCursor = 0u;

    /* 选择一个全空的非 active cluster 作为目标 */
    for (c = 0u; c < clusterNum; c++)
    {
        if (c == activeCluster)
        {
            continue;
        }
        if (clusterFullyErased(c))
        {
            target = c;
            break;
        }
    }
    if (target == 0xFFu)
    {
        return MINIFEE_ERR_FULL;
    }

    /* 搬运 active 内所有有效页到 target */
    for (p = 0u; p < pagesPerCluster; p++)
    {
        uint32 a = pageAddr(activeCluster, p);
        uint16 bid;
        uint16 dl;
        uint16 sq;
        uint32 crc;
        if (FlashDrv_Read(a, MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
        {
            return MINIFEE_ERR_FLASH;
        }
        if (!magicOk(pageBuf))
        {
            continue;
        }
        if (getU16(&pageBuf[OFF_STATUS]) != MINIFEE_STATUS_VALID)
        {
            continue; /* DIRTY/INVALID 忽略 */
        }
        bid = getU16(&pageBuf[OFF_BLOCKID]);
        if (bid >= numBlocks)
        {
            continue;
        }
        dl = getU16(&pageBuf[OFF_DATALEN]);
        if (dl > MINIFEE_PAGE_DATA_SIZE)
        {
            blockMap[bid].corrupt = FALSE; /* 损坏页随 GC 擦除，清除陈旧标志 */
            continue;
        }
        crc = MiniFee_CalcCrc(pageBuf, (uint32)(OFF_DATA + dl));
        if (getU32(&pageBuf[OFF_DATACRC]) != crc)
        {
            blockMap[bid].corrupt = FALSE; /* CRC 损坏页随 GC 擦除，清除陈旧标志 */
            continue;
        }
        sq = getU16(&pageBuf[OFF_SEQ]);
        /* 写入 target[tgtCursor]，保持 blockId/seq/dataLen 不变 */
        {
            uint32 ta = pageAddr(target, tgtCursor);
            MiniFee_ReturnType r = writePage(ta, bid, sq, dl, &pageBuf[OFF_DATA]);
            if (r != MINIFEE_OK)
            {
                return r;
            }
            blockMap[bid].pageAddr = ta; /* seq 不变，valid 不变 */
            tgtCursor++;
        }
    }

    /* 擦除原 active */
    if (FlashDrv_EraseCluster(activeCluster) != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    /* 切换 active 到 target */
    activeCluster = target;
    writeCursor = tgtCursor;
    return MINIFEE_OK;
}

/* ---- 启动扫描重建映射 + 定位 active/writeCursor ---- */
static MiniFee_ReturnType scanRebuild(void)
{
    uint16 b;
    uint8 c;
    uint16 p;
    boolean foundAny = FALSE;
    uint16 maxSeq = 0u;
    uint32 maxSeqAddr = 0u;

    for (b = 0u; b < numBlocks; b++)
    {
        blockMap[b].valid = FALSE;
        blockMap[b].corrupt = FALSE;
        blockMap[b].seq = 0u;
        blockMap[b].pageAddr = 0u;
    }

    for (c = 0u; c < clusterNum; c++)
    {
        for (p = 0u; p < pagesPerCluster; p++)
        {
            uint32 a = pageAddr(c, p);
            uint16 bid;
            uint16 dl;
            uint16 sq;
            uint32 crc;
            if (FlashDrv_Read(a, MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
            {
                return MINIFEE_ERR_FLASH;
            }
            if (!magicOk(pageBuf))
            {
                continue; /* 空页 */
            }
            if (getU16(&pageBuf[OFF_STATUS]) != MINIFEE_STATUS_VALID)
            {
                continue; /* 半写/未提交/已作废 → 丢弃（可丢最新一页） */
            }
            bid = getU16(&pageBuf[OFF_BLOCKID]);
            if (bid >= numBlocks)
            {
                continue;
            }
            dl = getU16(&pageBuf[OFF_DATALEN]);
            if (dl > MINIFEE_PAGE_DATA_SIZE)
            {
                blockMap[bid].corrupt = TRUE; /* dataLen 非法 → 视为损坏 */
                continue;
            }
            crc = MiniFee_CalcCrc(pageBuf, (uint32)(OFF_DATA + dl));
            if (getU32(&pageBuf[OFF_DATACRC]) != crc)
            {
                /* VALID 状态但 CRC 损坏：标记 corrupt 以便上层区分，自身丢弃 */
                blockMap[bid].corrupt = TRUE;
                continue;
            }
            sq = getU16(&pageBuf[OFF_SEQ]);
            if ((!blockMap[bid].valid) || isNewer(sq, blockMap[bid].seq))
            {
                blockMap[bid].valid = TRUE;
                blockMap[bid].corrupt = FALSE; /* 找到有效页，清除损坏标记 */
                blockMap[bid].seq = sq;
                blockMap[bid].pageAddr = a;
            }
            if ((!foundAny) || isNewer(sq, maxSeq))
            {
                foundAny = TRUE;
                maxSeq = sq;
                maxSeqAddr = a;
            }
        }
    }

    activeCluster = (foundAny) ? (uint8)(maxSeqAddr / flashProp.clusterSize) : 0u;

    /* writeCursor = active 内首个空页索引；全满则为 pagesPerCluster */
    writeCursor = pagesPerCluster;
    for (p = 0u; p < pagesPerCluster; p++)
    {
        uint8 hdr[4];
        if (FlashDrv_Read(pageAddr(activeCluster, p), 4u, hdr) != FLASH_OK)
        {
            return MINIFEE_ERR_FLASH;
        }
        if (!magicOk(hdr))
        {
            writeCursor = p;
            break;
        }
    }
    return MINIFEE_OK;
}

/* ---- 对外 API ---- */

MiniFee_ReturnType MiniFee_Init(uint16 numBlk)
{
    FlashDrv_PropertyType prop;

    if (FlashDrv_Init() != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    if (FlashDrv_GetProperty(&prop) != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    /* 配置与 Flash 属性一致性校验（P4 对齐要求） */
    if (prop.pageSize != (uint32)MINIFEE_PAGE_SIZE)
    {
        return MINIFEE_ERR_PARAM;
    }
    if (prop.clusterSize != (uint32)MINIFEE_CLUSTER_SIZE)
    {
        return MINIFEE_ERR_PARAM;
    }
    if (prop.clusterCount != (uint32)MINIFEE_CLUSTER_NUM)
    {
        return MINIFEE_ERR_PARAM;
    }
    if ((numBlk == 0u) || (numBlk > MINIFEE_MAX_NUM_BLOCKS))
    {
        return MINIFEE_ERR_PARAM;
    }
    flashProp = prop;
    clusterNum = (uint8)prop.clusterCount;
    pagesPerCluster = (uint16)(prop.clusterSize / prop.pageSize);
    if (pagesPerCluster == 0u)
    {
        return MINIFEE_ERR_PARAM;
    }
    /* Model A 容量约束：单 active cluster 须容纳全部 live 块 + 1 空闲 */
    if (pagesPerCluster <= numBlk)
    {
        return MINIFEE_ERR_PARAM;
    }
    numBlocks = numBlk;

    {
        MiniFee_ReturnType r = scanRebuild();
        if (r != MINIFEE_OK)
        {
            inited = FALSE;
            return r;
        }
    }
    inited = TRUE;
    return MINIFEE_OK;
}

MiniFee_ReturnType MiniFee_ReadBlock(uint16 blockId, uint8 *dest, uint16 *dataLen)
{
    uint16 dl;
    uint32 crc;
    uint16 i;

    if (!inited)
    {
        return MINIFEE_ERR_PARAM;
    }
    if ((blockId >= numBlocks) || (dest == NULL_PTR))
    {
        return MINIFEE_ERR_PARAM;
    }
    if (!blockMap[blockId].valid)
    {
        /* 扫描时发现损坏页（VALID 状态但 CRC 损坏）→ 上报 CRC 错误，便于 MiniNvm 区分 */
        if (blockMap[blockId].corrupt)
        {
            return MINIFEE_ERR_CRC;
        }
        return MINIFEE_ERR_NOT_FOUND;
    }
    if (FlashDrv_Read(blockMap[blockId].pageAddr, MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    dl = getU16(&pageBuf[OFF_DATALEN]);
    if (dl > MINIFEE_PAGE_DATA_SIZE)
    {
        return MINIFEE_ERR_CRC;
    }
    crc = MiniFee_CalcCrc(pageBuf, (uint32)(OFF_DATA + dl));
    if (getU32(&pageBuf[OFF_DATACRC]) != crc)
    {
        return MINIFEE_ERR_CRC;
    }
    for (i = 0u; i < dl; i++)
    {
        dest[i] = pageBuf[OFF_DATA + i];
    }
    if (dataLen != NULL_PTR)
    {
        *dataLen = dl;
    }
    return MINIFEE_OK;
}

MiniFee_ReturnType MiniFee_WriteBlock(uint16 blockId, const uint8 *src, uint16 len)
{
    uint16 newSeq;
    uint32 newAddr;
    uint32 oldAddr;
    boolean hadOld;
    MiniFee_ReturnType r;

    if (!inited)
    {
        return MINIFEE_ERR_PARAM;
    }
    if ((blockId >= numBlocks) || (src == NULL_PTR) || (len > MINIFEE_PAGE_DATA_SIZE))
    {
        return MINIFEE_ERR_PARAM;
    }

    /* 当前写 cluster 写满 → 触发 GC */
    if (writeCursor >= pagesPerCluster)
    {
        r = gcAndSwitch();
        if (r != MINIFEE_OK)
        {
            return r;
        }
    }

    newSeq = (blockMap[blockId].valid) ? (uint16)(blockMap[blockId].seq + 1u) : 0u;
    newAddr = pageAddr(activeCluster, writeCursor);

    /* 写新页并提交 */
    r = writePage(newAddr, blockId, newSeq, len, src);
    if (r != MINIFEE_OK)
    {
        return r;
    }

    hadOld = blockMap[blockId].valid;
    oldAddr = blockMap[blockId].pageAddr;
    blockMap[blockId].valid = TRUE;
    blockMap[blockId].corrupt = FALSE; /* 新写入清损坏标记 */
    blockMap[blockId].pageAddr = newAddr;
    blockMap[blockId].seq = newSeq;
    writeCursor++;

    /* 提交成功后再作废旧页（崩溃可残留两份有效页，恢复取最大 seq） */
    if (hadOld)
    {
        (void)invalidatePage(oldAddr);
    }
    return MINIFEE_OK;
}

MiniFee_ReturnType MiniFee_EraseBlock(uint16 blockId)
{
    MiniFee_ReturnType r;
    if (!inited)
    {
        return MINIFEE_ERR_PARAM;
    }
    if (blockId >= numBlocks)
    {
        return MINIFEE_ERR_PARAM;
    }
    if (!blockMap[blockId].valid)
    {
        return MINIFEE_OK; /* 幂等：无有效页即视为已擦 */
    }
    r = invalidatePage(blockMap[blockId].pageAddr);
    if (r != MINIFEE_OK)
    {
        return r;
    }
    blockMap[blockId].valid = FALSE;
    blockMap[blockId].corrupt = FALSE;
    blockMap[blockId].seq = 0u;
    return MINIFEE_OK;
}

uint16 MiniFee_GetPageDataSize(void)
{
    return MINIFEE_PAGE_DATA_SIZE;
}

uint16 MiniFee_GetClusterCount(void)
{
    return (uint16)clusterNum;
}

uint16 MiniFee_GetNumBlocks(void)
{
    return numBlocks;
}
