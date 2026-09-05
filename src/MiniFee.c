/**
 * \file MiniFee.c
 * \brief MiniFee（Flash EEPROM 模拟）实现
 * \details 设计要点详见 docs/02_minifee_design.md：
 *          - Flash 布局：clusterCount 个 cluster，每 cluster pagesPerCluster 个页；
 *          - 页结构：[页头10B][数据区 PAGE_DATA_SIZE][页尾6B: dataCrc(4)+status(2)]；
 *          - 状态机（Flash 1→0 友好）：ERASED(0xFFFF) → VALID(0x5555,提交) → INVALID(0x0000,作废)；
 *            提交位最后写，未提交页在掉电恢复时判为半写丢弃 → 满足"可丢最新一页"；
 *          - 磨损均衡：所有写都落到当前 active cluster；写满触发 GC，把有效页搬到目标 cluster，
 *            擦除原 cluster，目标 cluster 成为新 active，cluster 轮转 0→1→…→N-1→0；
 *          - 掉电恢复：启动扫描全部页，按 Magic/Status(VALID)/CRC 重建 blockId→最新页映射
 *            （同块取最大 seq），active 取全局最大 seq 页所在 cluster，writeCursor 取首个空页。
 *
 *          全同步、纯 C99、无动态内存、OS 无关。
 * \req P0 #4（多 cluster）、#5（块↔页映射）、#6（磨损均衡）、#7（GC）、#9（可丢最新一页）、#10（简化 CRC）
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
/** \brief blockMap 条目：块→当前有效页映射及扫描时发现的损坏标志。 */
typedef struct
{
    boolean valid;
    boolean corrupt;   /* 扫描时发现该块存在 VALID 状态但 CRC 损坏的页 */
    uint32  pageAddr;   /* 该块当前有效页的扁平地址 */
    uint16  seq;
} BlockMapEntry;

/* ---- 模块静态状态 ---- */
/** 模块静态状态：Flash 属性、页/块规模、active cluster 与写游标、块映射表、整页缓冲。全同步非可重入。 */
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
/**
 * \brief 16 位值按小端序写入 2 字节缓冲。
 * \param[out] p 目标缓冲（>= 2 字节）
 * \param[in] v 待写入值
 */
static void putU16(uint8 *p, uint16 v)
{
    p[0] = (uint8)(v & 0xFFu);
    p[1] = (uint8)((v >> 8) & 0xFFu);
}
/**
 * \brief 32 位值按小端序写入 4 字节缓冲。
 * \param[out] p 目标缓冲（>= 4 字节）
 * \param[in] v 待写入值
 */
static void putU32(uint8 *p, uint32 v)
{
    p[0] = (uint8)(v & 0xFFu);
    p[1] = (uint8)((v >> 8) & 0xFFu);
    p[2] = (uint8)((v >> 16) & 0xFFu);
    p[3] = (uint8)((v >> 24) & 0xFFu);
}
/**
 * \brief 从 2 字节缓冲按小端序读出 16 位值。
 * \param[in] p 源缓冲（>= 2 字节）
 * \return 组合后的 16 位值
 */
static uint16 getU16(const uint8 *p)
{
    return (uint16)((uint16)p[0] | ((uint16)p[1] << 8));
}
/**
 * \brief 从 4 字节缓冲按小端序读出 32 位值。
 * \param[in] p 源缓冲（>= 4 字节）
 * \return 组合后的 32 位值
 */
static uint32 getU32(const uint8 *p)
{
    return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}
/**
 * \brief 校验页头 4 字节魔数是否为 "MFEE"。
 * \param[in] buf 页头缓冲（>= 4 字节）
 * \return TRUE：魔数匹配；FALSE：不匹配（空页/损坏）。
 */
static boolean magicOk(const uint8 *buf)
{
    return (buf[0] == MINIFEE_MAGIC0) && (buf[1] == MINIFEE_MAGIC1) &&
           (buf[2] == MINIFEE_MAGIC2) && (buf[3] == MINIFEE_MAGIC3);
}

/* ---- 地址计算 ---- */
/**
 * \brief 计算指定 cluster 的扁平基地址。
 * \param[in] c cluster 索引 [0, clusterNum)
 * \return 基地址 = c * clusterSize
 */
static uint32 clusterBase(uint8 c)
{
    return (uint32)c * flashProp.clusterSize;
}
/**
 * \brief 计算指定 cluster 内某页的扁平基地址。
 * \param[in] c cluster 索引 [0, clusterNum)
 * \param[in] p 页索引 [0, pagesPerCluster)
 * \return 页基地址 = clusterBase(c) + p * pageSize
 */
static uint32 pageAddr(uint8 c, uint16 p)
{
    return clusterBase(c) + (uint32)p * flashProp.pageSize;
}

/* ---- CRC ---- */
/**
 * \brief 软件位运算 CRC（非查表），算法由 MINIFEE_CRC_TYPE 在 CRC32/CRC16 间选择。
 * \details 覆盖 [data, data+len)；参数（多项式/初值/输出异或）来自 MiniFee_Cfg.h。
 * \param[in] data 数据起始
 * \param[in] len 覆盖字节数
 * \return CRC 结果（CRC16 时零扩展为 32 位）
 * \req P0 #10
 */
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
/**
 * \brief 序列号新旧判定（uint16 回绕安全）。
 * \details 有符号差值法：差值在 ±32768 内时符号即新旧；回绕边界附近判定受限，
 *          bootloader 写入频次远低于回绕阈值，可接受（docs/06 风险清单）。
 * \param[in] a 候选序列号
 * \param[in] b 基准序列号
 * \return TRUE：a 比 b 新；FALSE：a 不比 b 新。
 */
static boolean isNewer(uint16 a, uint16 b)
{
    return (((int16)(a - b)) > 0) ? TRUE : FALSE;
}

/**
 * \brief 单页写入：页头+数据 → dataCrc → 提交 VALID（三步次序，Flash 1→0 友好）。
 * \details 提交字最后写：前两步与第三步之间掉电，该页 status 仍为 ERASED，
 *          恢复扫描判为半写页丢弃 → 满足"可丢最新一页"。
 * \param[in] addr 页基地址
 * \param[in] blockId 块 ID
 * \param[in] seq 序列号
 * \param[in] dataLen 数据长度（≤ MINIFEE_PAGE_DATA_SIZE）
 * \param[in] data 源数据
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FLASH：任一步底层写失败。
 * \req P0 #9
 */
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

/**
 * \brief 作废页：将页状态字写为 INVALID（0x0000）。
 * \param[in] addr 页基地址
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FLASH：底层写失败。
 */
static MiniFee_ReturnType invalidatePage(uint32 addr)
{
    uint8 stBuf[2];
    putU16(stBuf, MINIFEE_STATUS_INVALID);
    return (FlashDrv_Write(addr + OFF_STATUS, 2u, stBuf) == FLASH_OK) ? MINIFEE_OK : MINIFEE_ERR_FLASH;
}

/**
 * \brief 判断指定 cluster 是否全空（所有页均无魔数）。
 * \param[in] c cluster 索引 [0, clusterNum)
 * \return TRUE：全空（可作 GC 目标）；FALSE：存在已写页或底层读失败。
 */
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

/**
 * \brief GC：把 active cluster 内所有有效页搬到全空目标 cluster，擦除原 active 并切换。
 * \details 仅搬运 VALID 且 CRC 通过的页，blockId/seq/dataLen 原样保持；损坏页随搬运丢弃并
 *          清除陈旧 corrupt 标志。搬运中/擦除前掉电可能残留两个 cluster 同块有效页，
 *          恢复按最大 seq 取新（docs/06 风险清单）。
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FULL：无全空目标 cluster；MINIFEE_ERR_FLASH：底层失败。
 * \req P0 #6、#7
 */
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

/**
 * \brief 启动扫描：遍历全部页重建 blockMap，并定位 active cluster 与 writeCursor。
 * \details 只接受魔数匹配、status=VALID 且 CRC 通过的页；同块多页取最大 seq；
 *          半写/作废/损坏页丢弃（可丢最新一页）。active 取全局最大 seq 页所在 cluster；
 *          writeCursor 取 active 内首个空页，全满则为 pagesPerCluster（下次写触发 GC）。
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FLASH：底层读失败。
 * \req P0 #9
 */
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

/**
 * \brief 初始化：FlashDrv_Init + GetProperty → 逐项校验配置一致性 → 校验 numBlocks
 *        （> 0、≤上限、< pagesPerCluster）→ scanRebuild 重建映射。
 * \req P0 #4、#9、TC-F-01、TC-F-09
 */
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

/**
 * \brief 读块：无映射时按 corrupt 标志区分 ERR_CRC（扫描发现损坏页）与 NOT_FOUND；
 *        命中映射则整页读入、二次 CRC 校验后拷贝数据区。
 * \req P0 #5、#10、TC-F-02、TC-F-05
 */
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

/**
 * \brief 写块：active 写满先 gcAndSwitch；新页 seq = 旧 seq+1（无旧页为 0）；
 *        writePage 提交成功后更新 blockMap 并作废旧页（崩溃残留双有效页由恢复取最大 seq 化解）。
 * \req P0 #5、#6、#7、#9、TC-F-03、TC-F-07
 */
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

/**
 * \brief 擦除块：作废映射中的有效页（幂等）；status 写失败时保持原映射。
 * \req P0 #11、TC-F-06
 */
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

/**
 * \brief 查询页数据区大小：直接返回编译期常量 MINIFEE_PAGE_DATA_SIZE。
 * \req TC-F-01
 */
uint16 MiniFee_GetPageDataSize(void)
{
    return MINIFEE_PAGE_DATA_SIZE;
}

/**
 * \brief 查询 cluster 数量：返回 Init 时记录的 Flash 属性值。
 * \req TC-F-01
 */
uint16 MiniFee_GetClusterCount(void)
{
    return (uint16)clusterNum;
}

/**
 * \brief 查询当前块数：返回 Init 时传入并校验的实际值。
 * \req TC-F-01
 */
uint16 MiniFee_GetNumBlocks(void)
{
    return numBlocks;
}
