/**
 * \file MiniFee.c
 * \brief MiniFee（Flash EEPROM 模拟）实现
 * \details 设计要点详见 docs/02_minifee_design.md：
 *          - 写入粒度：MINIFEE_PAGE_SIZE = 物理 Flash 最小编程字节数；所有 FlashDrv_Write
 *            均为整页单次编程（ECC 保守模型，每页只写一次、写满、不足填 0xFF）；
 *          - 块槽（逐块变长）：块 b 槽占 blockPages = size/PAGE_SIZE + 2 页
 *            [header 页(magic+blockId+seq)][数据页...][提交页(status+dataLen+crc)]，
 *            槽不跨 cluster；
 *          - 写次序：header 页 → 数据页 → 提交页（最后写 = 提交点）；提交页未写的槽
 *            在掉电恢复时判为半写丢弃 → 满足"可丢最新一槽"（无 INVALID 二次写作废）；
 *          - 磨损均衡（按 cluster）：所有写追加到 active cluster 写游标处；槽放不下时
 *            触发 GC，把全部 live 槽（含墓碑/孤岛）搬到目标 cluster，擦除原 cluster
 *            及无映射槽的垃圾 cluster，cluster 轮转 0→1→…→N-1→0；
 *          - 擦除块 = 写墓碑槽（dataLen=0，seq+1，幂等）；
 *          - 掉电恢复：逐 cluster 混合推进扫描（无 magic 逐页 +1，有 magic 整槽 +P），
 *            同块取最大 seq 重建映射；active 取全局最大 seq 槽所在 cluster，
 *            写游标取该 cluster 扫描推进终点。
 *
 *          全同步、纯 C99、无动态内存、OS 无关。
 * \req P0 #4（多 cluster）、#5（块↔槽映射）、#6（磨损均衡）、#7（GC）、#9（可丢最新一槽）、#10（简化 CRC）、#11（擦除）
 */

#include "MiniFee.h"
#include "MiniFee_Cfg.h"
#include "FlashDrv.h"
#include <string.h>

/* ---- 槽内字段偏移（派生自布局，docs/02 §2） ---- */
#define OFF_MAGIC        0u    /* header 页内魔数偏移 */
#define OFF_BLOCKID      4u    /* header 页内块 ID 偏移 */
#define OFF_SEQ          6u    /* header 页内序号偏移 */
#define OFF_STATUS       0u    /* 提交页内状态字偏移 */
#define OFF_DATALEN      2u    /* 提交页内数据长度偏移 */
#define OFF_DATACRC      4u    /* 提交页内 CRC 偏移 */
#define WBUF_OFF_DATALEN MINIFEE_PAGE_SIZE        /* 工作缓冲内 dataLen 拼接偏移（header 页之后） */
#define WBUF_OFF_DATA    (MINIFEE_PAGE_SIZE + 2u) /* 工作缓冲内数据起始（[header 页][dataLen][data] 布局） */

/* ---- 内部槽读取结果 ---- */
/** \brief readSlot 结果码：OK=已提交且 CRC 通过；NOT_COMMITTED=提交页未写；CORRUPT=结构/CRC 损坏；FLASH=底层读失败。 */
typedef enum
{
    SLOT_OK = 0,
    SLOT_NOT_COMMITTED,
    SLOT_CORRUPT,
    SLOT_FLASH
} SlotStatus;

/* ---- 内部映射表条目 ---- */
/** \brief blockMap 条目：块→当前槽（含墓碑槽）映射及扫描时发现的损坏标志。 */
typedef struct
{
    boolean valid;
    boolean corrupt;   /* 扫描时发现该块存在已提交但 CRC 损坏的槽 */
    uint32  pageAddr;  /* 该块当前槽首页的扁平地址 */
    uint16  seq;
} BlockMapEntry;

/* ---- 模块静态状态 ---- */
/** 模块静态状态：Flash 属性、页规模、逐块大小与槽数、active cluster 与写游标（页索引）、块映射表、工作缓冲。全同步非可重入。 */
static FlashDrv_PropertyType flashProp;
static uint8  clusterNum;
static uint16 pagesPerCluster;
static uint16 numBlocks;
static uint8  activeCluster;
static uint16 writeCursor;   /* active cluster 内下一可用页索引（槽追加起点） */
static boolean inited = FALSE;
static uint16 blockSizeTbl[MINIFEE_MAX_NUM_BLOCKS]; /* 逐块数据大小（Init 传入，容量上限） */
static uint16 blockPages[MINIFEE_MAX_NUM_BLOCKS];   /* 逐块槽页数 = size/PAGE_SIZE + 2 */
static BlockMapEntry blockMap[MINIFEE_MAX_NUM_BLOCKS];
static uint8 workBuf[MINIFEE_WORK_BUF_SIZE]; /* CRC 拼接缓冲：[header 页][dataLen][data] */
static uint8 pageBuf[MINIFEE_PAGE_SIZE];     /* 单页构建缓冲 */

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
 * \brief 校验缓冲前 4 字节魔数是否为 "MFEE"。
 * \param[in] buf 页头缓冲（>= 4 字节）
 * \return TRUE：魔数匹配；FALSE：不匹配（空页/损坏）。
 */
static boolean magicOk(const uint8 *buf)
{
    return ((buf[0] == MINIFEE_MAGIC0) && (buf[1] == MINIFEE_MAGIC1) &&
            (buf[2] == MINIFEE_MAGIC2) && (buf[3] == MINIFEE_MAGIC3)) ? TRUE : FALSE;
}
/**
 * \brief 以 0xFF 填充单页构建缓冲 pageBuf（擦除态填充，对 1→0 约束为空操作）。
 */
static void fillPageErased(void)
{
    uint16 i;
    for (i = 0u; i < MINIFEE_PAGE_SIZE; i++)
    {
        pageBuf[i] = 0xFFu;
    }
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
/**
 * \brief 计算指定槽首页地址所属的 cluster 索引。
 * \param[in] addr 槽首页扁平地址
 * \return cluster 索引 [0, clusterNum)
 */
static uint8 clusterOf(uint32 addr)
{
    return (uint8)(addr / flashProp.clusterSize);
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
 * \brief 整槽写入：header 页 → 数据页 → 提交页（逐页整页单次编程，提交页最后写）。
 * \details 提交页最后写：此前任一步掉电，提交页仍为 0xFF（ERASED），恢复扫描判为
 *          半写槽丢弃 → 满足"可丢最新一槽"。dataLen=0 时为墓碑槽，不写数据页。
 *          CRC 覆盖 [header 页 S 字节][dataLen 2 字节][data dataLen 字节] 的拼接，
 *          拼接布局即 workBuf（读取校验按相同布局重算）。
 * \param[in] slotAddr 槽首页地址（页对齐）
 * \param[in] blockId 块 ID
 * \param[in] seq 序列号
 * \param[in] dataLen 数据长度（≤ 该块槽数据区大小；0 = 墓碑）
 * \param[in] data 源数据（dataLen=0 时可为 NULL_PTR；位于 workBuf 数据区时为自拷贝）
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FLASH：任一步底层写失败。
 * \req P0 #9
 */
static MiniFee_ReturnType writeSlot(uint32 slotAddr, uint16 blockId, uint16 seq,
                                    uint16 dataLen, const uint8 *data)
{
    uint16 pb = blockPages[blockId];
    uint16 dataPages = (uint16)(((uint32)dataLen + (uint32)MINIFEE_PAGE_SIZE - 1u) /
                                (uint32)MINIFEE_PAGE_SIZE);
    uint32 crc;
    uint16 p;
    uint16 i;

    /* 在 workBuf 中拼接 [header 页][dataLen][data] 并计算 CRC */
    for (i = 0u; i < MINIFEE_PAGE_SIZE; i++)
    {
        workBuf[i] = 0xFFu;
    }
    workBuf[OFF_MAGIC + 0u] = MINIFEE_MAGIC0;
    workBuf[OFF_MAGIC + 1u] = MINIFEE_MAGIC1;
    workBuf[OFF_MAGIC + 2u] = MINIFEE_MAGIC2;
    workBuf[OFF_MAGIC + 3u] = MINIFEE_MAGIC3;
    putU16(&workBuf[OFF_BLOCKID], blockId);
    putU16(&workBuf[OFF_SEQ], seq);
    putU16(&workBuf[WBUF_OFF_DATALEN], dataLen);
    for (i = 0u; i < dataLen; i++)
    {
        workBuf[WBUF_OFF_DATA + i] = data[i]; /* data == 目标区时为自赋值，无害 */
    }
    crc = MiniFee_CalcCrc(workBuf, (uint32)(WBUF_OFF_DATA + dataLen));

    /* 1) 整页写 header 页 */
    if (FlashDrv_Write(slotAddr, MINIFEE_PAGE_SIZE, workBuf) != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }

    /* 2) 逐页写数据页（尾部 0xFF 填充；dataLen=0 时无数据页） */
    for (p = 0u; p < dataPages; p++)
    {
        uint16 off = (uint16)((uint32)p * (uint32)MINIFEE_PAGE_SIZE);
        uint16 remain = (uint16)(dataLen - off);
        fillPageErased();
        for (i = 0u; (i < MINIFEE_PAGE_SIZE) && (i < remain); i++)
        {
            pageBuf[i] = data[off + i];
        }
        if (FlashDrv_Write(slotAddr + (uint32)MINIFEE_PAGE_SIZE + (uint32)off,
                           MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
        {
            return MINIFEE_ERR_FLASH;
        }
    }

    /* 3) 整页写提交页（status=VALID，提交点） */
    fillPageErased();
    putU16(&pageBuf[OFF_STATUS], MINIFEE_STATUS_VALID);
    putU16(&pageBuf[OFF_DATALEN], dataLen);
    putU32(&pageBuf[OFF_DATACRC], crc);
    if (FlashDrv_Write(slotAddr + (uint32)(pb - 1u) * (uint32)MINIFEE_PAGE_SIZE,
                       MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    return MINIFEE_OK;
}

/**
 * \brief 整槽读取与校验：header 页 → 提交页 → 数据页，重算 CRC 比对。
 * \details 校验通过后 workBuf 内即 [header 页][dataLen][data] 布局，数据可经
 *          &workBuf[WBUF_OFF_DATA] 访问（供 GC 搬运/ReadBlock 拷贝）。
 * \param[in] slotAddr 槽首页地址（页对齐）
 * \param[in] blockId 期望的块 ID（与 header 页比对，防错位）
 * \param[out] seq 槽序列号（可为 NULL_PTR，不关心时）
 * \param[out] dataLen 数据长度（可为 NULL_PTR，不关心时）
 * \return SLOT_OK：已提交且校验通过；SLOT_NOT_COMMITTED：提交页未写（半写槽）；
 *         SLOT_CORRUPT：header 不匹配/dataLen 越界/CRC 损坏；SLOT_FLASH：底层读失败。
 */
static SlotStatus readSlot(uint32 slotAddr, uint16 blockId, uint16 *seq, uint16 *dataLen)
{
    uint16 pb = blockPages[blockId];
    uint16 dl;
    uint32 crc;

    /* header 页 */
    if (FlashDrv_Read(slotAddr, MINIFEE_PAGE_SIZE, workBuf) != FLASH_OK)
    {
        return SLOT_FLASH;
    }
    if ((!magicOk(workBuf)) || (getU16(&workBuf[OFF_BLOCKID]) != blockId))
    {
        return SLOT_CORRUPT;
    }

    /* 提交页（槽末页） */
    if (FlashDrv_Read(slotAddr + (uint32)(pb - 1u) * (uint32)MINIFEE_PAGE_SIZE,
                      MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
    {
        return SLOT_FLASH;
    }
    if (getU16(&pageBuf[OFF_STATUS]) != MINIFEE_STATUS_VALID)
    {
        return SLOT_NOT_COMMITTED; /* 半写/未提交 → 掉电残留 */
    }
    dl = getU16(&pageBuf[OFF_DATALEN]);
    if (dl > blockSizeTbl[blockId])
    {
        return SLOT_CORRUPT;
    }

    /* 数据页（连续读 dl 字节）并按 [header 页][dataLen][data] 布局重算 CRC */
    putU16(&workBuf[WBUF_OFF_DATALEN], dl);
    if (dl > 0u)
    {
        if (FlashDrv_Read(slotAddr + (uint32)MINIFEE_PAGE_SIZE, dl,
                          &workBuf[WBUF_OFF_DATA]) != FLASH_OK)
        {
            return SLOT_FLASH;
        }
    }
    crc = MiniFee_CalcCrc(workBuf, (uint32)(WBUF_OFF_DATA + dl));
    if (crc != getU32(&pageBuf[OFF_DATACRC]))
    {
        return SLOT_CORRUPT;
    }
    if (seq != NULL_PTR)
    {
        *seq = getU16(&workBuf[OFF_SEQ]);
    }
    if (dataLen != NULL_PTR)
    {
        *dataLen = dl;
    }
    return SLOT_OK;
}

/**
 * \brief 判断指定 cluster 是否被任一映射槽占用（含 live 数据槽/墓碑槽）。
 * \param[in] c cluster 索引 [0, clusterNum)
 * \return TRUE：存在映射槽指向该 cluster；FALSE：无。
 */
static boolean clusterHasMappedSlots(uint8 c)
{
    uint16 b;
    for (b = 0u; b < numBlocks; b++)
    {
        if ((blockMap[b].valid) && (clusterOf(blockMap[b].pageAddr) == c))
        {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * \brief 判断指定 cluster 是否全空（所有字节均为 0xFF，从未编程）。
 * \param[in] c cluster 索引 [0, clusterNum)
 * \return TRUE：全空；FALSE：存在已编程内容或底层读失败。
 */
static boolean clusterFullyErased(uint8 c)
{
    uint32 base = clusterBase(c);
    uint32 off = 0u;
    while (off < flashProp.clusterSize)
    {
        uint32 remain = flashProp.clusterSize - off;
        uint16 chunk = (uint16)((remain > (uint32)MINIFEE_WORK_BUF_SIZE)
                                ? (uint32)MINIFEE_WORK_BUF_SIZE : remain);
        uint16 i;
        if (FlashDrv_Read(base + off, chunk, workBuf) != FLASH_OK)
        {
            return FALSE;
        }
        for (i = 0u; i < chunk; i++)
        {
            if (workBuf[i] != 0xFFu)
            {
                return FALSE;
            }
        }
        off += (uint32)chunk;
    }
    return TRUE;
}

/**
 * \brief GC：把全部 live 槽（按映射，含墓碑与孤岛槽）搬到目标 cluster，
 *        擦除原 active 及无映射槽的垃圾 cluster 并切换 active。
 * \details 目标 cluster 选择"非 active 且无映射槽"者（含 GC 中断残留的半搬 cluster，
 *          先擦后用，碎片自愈）；搬运逐块提交，中途掉电由恢复按最大 seq 取新化解；
 *          映射槽损坏时不搬运并标记 corrupt，该块数据随原 cluster 擦除丢弃。
 *          全空 cluster 跳过擦除以避免无意义磨损。
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FULL：无可用目标 cluster；MINIFEE_ERR_FLASH：底层失败。
 * \req P0 #6、#7
 */
static MiniFee_ReturnType gcAndSwitch(void)
{
    uint8 target = 0xFFu;
    uint8 c;
    uint16 b;
    uint16 tgtCursor = 0u; /* 目标 cluster 内页游标 */

    /* 选择非 active 且无映射槽的目标 cluster */
    for (c = 0u; c < clusterNum; c++)
    {
        if ((c == activeCluster) || (clusterHasMappedSlots(c)))
        {
            continue;
        }
        target = c;
        break;
    }
    if (target == 0xFFu)
    {
        return MINIFEE_ERR_FULL;
    }
    /* 目标非全空（GC 中断残留的半搬内容）则先擦除 */
    if ((!clusterFullyErased(target)) && (FlashDrv_EraseCluster(target) != FLASH_OK))
    {
        return MINIFEE_ERR_FLASH;
    }

    /* 按块序搬运全部映射槽（含墓碑/孤岛），原样保持 blockId/seq/dataLen */
    for (b = 0u; b < numBlocks; b++)
    {
        uint16 sq;
        uint16 dl;
        SlotStatus st;
        if (!blockMap[b].valid)
        {
            continue;
        }
        st = readSlot(blockMap[b].pageAddr, b, &sq, &dl);
        if (st == SLOT_FLASH)
        {
            return MINIFEE_ERR_FLASH;
        }
        if (st != SLOT_OK)
        {
            /* 映射槽损坏/未提交：不搬运并标记损坏，该块随原 cluster 擦除丢弃 */
            blockMap[b].valid = FALSE;
            blockMap[b].corrupt = TRUE;
            continue;
        }
        {
            uint32 ta = pageAddr(target, tgtCursor);
            MiniFee_ReturnType r = writeSlot(ta, b, sq, dl, &workBuf[WBUF_OFF_DATA]);
            if (r != MINIFEE_OK)
            {
                return r;
            }
            blockMap[b].pageAddr = ta; /* seq/valid 不变 */
            tgtCursor = (uint16)(tgtCursor + blockPages[b]);
        }
    }

    /* 擦除原 active 及其他无映射槽且非全空的 cluster（垃圾回收，碎片自愈） */
    for (c = 0u; c < clusterNum; c++)
    {
        if ((c == target) || (clusterHasMappedSlots(c)))
        {
            continue;
        }
        if (!clusterFullyErased(c))
        {
            if (FlashDrv_EraseCluster(c) != FLASH_OK)
            {
                return MINIFEE_ERR_FLASH;
            }
        }
    }

    activeCluster = target;
    writeCursor = tgtCursor;
    return MINIFEE_OK;
}

/**
 * \brief 启动扫描：逐 cluster 混合推进重建 blockMap，定位 active cluster 与写游标。
 * \details 页粒度游标 p：页首无魔数 → p+1（空页/无 header 残留页）；有魔数 → 按块表
 *          定槽界（blockPages[bid] 页）整槽校验后 p+P 跳过（半写/损坏槽同样整体跳过，
 *          不复用其已编程页）。只接受魔数匹配、提交页 VALID 且 CRC 通过的槽；
 *          同块多槽取最大 seq（墓碑参与竞争）。active 取全局最大 seq 槽所在 cluster；
 *          writeCursor 取该 cluster 的扫描推进终点。
 * \return MINIFEE_OK：成功；MINIFEE_ERR_FLASH：底层读失败。
 * \req P0 #9
 */
static MiniFee_ReturnType scanRebuild(void)
{
    uint16 b;
    uint8 c;
    uint16 endPage[MINIFEE_CLUSTER_NUM];
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
        uint16 p = 0u;
        while (p < pagesPerCluster)
        {
            uint32 a = pageAddr(c, p);
            uint8 hdr[4];
            uint16 bid;
            if (FlashDrv_Read(a, 4u, hdr) != FLASH_OK)
            {
                return MINIFEE_ERR_FLASH;
            }
            if (!magicOk(hdr))
            {
                p++; /* 空页/无 header 残留页，逐页推进 */
                continue;
            }
            /* 槽首页：读 header 页取 blockId 定槽界 */
            if (FlashDrv_Read(a, MINIFEE_PAGE_SIZE, workBuf) != FLASH_OK)
            {
                return MINIFEE_ERR_FLASH;
            }
            bid = getU16(&workBuf[OFF_BLOCKID]);
            if ((bid >= numBlocks) || ((p + blockPages[bid]) > pagesPerCluster))
            {
                p++; /* 块号越界/槽界越界：无法定界，逐页跳过（人工介入场景） */
                continue;
            }
            {
                uint16 sq;
                uint16 dl;
                SlotStatus st = readSlot(a, bid, &sq, &dl);
                switch (st)
                {
                    case SLOT_OK:
                        if ((!blockMap[bid].valid) || isNewer(sq, blockMap[bid].seq))
                        {
                            blockMap[bid].valid = TRUE;
                            blockMap[bid].corrupt = FALSE; /* 找到有效槽，清除损坏标记 */
                            blockMap[bid].seq = sq;
                            blockMap[bid].pageAddr = a;
                        }
                        if ((!foundAny) || isNewer(sq, maxSeq))
                        {
                            foundAny = TRUE;
                            maxSeq = sq;
                            maxSeqAddr = a;
                        }
                        break;
                    case SLOT_NOT_COMMITTED:
                        break; /* 半写槽：丢弃（可丢最新一槽） */
                    case SLOT_CORRUPT:
                        if (!blockMap[bid].valid)
                        {
                            blockMap[bid].corrupt = TRUE;
                        }
                        break;
                    case SLOT_FLASH:
                    default:
                        return MINIFEE_ERR_FLASH;
                }
                p = (uint16)(p + blockPages[bid]); /* 整槽推进（含半写/损坏槽） */
            }
        }
        endPage[c] = p; /* p == pagesPerCluster 表示该 cluster 已无整槽可用 */
    }

    activeCluster = (foundAny) ? clusterOf(maxSeqAddr) : 0u;
    writeCursor = endPage[activeCluster];
    return MINIFEE_OK;
}

/* ---- 对外 API ---- */

/**
 * \brief 初始化：FlashDrv_Init + GetProperty → 逐项校验配置一致性 → 校验块表
 *        （逐块 size 为 PAGE_SIZE 整数倍、≤MAX_BLOCK_SIZE；容量 ΣblockPages+max ≤
 *        pagesPerCluster）→ scanRebuild 重建映射。
 * \req P0 #4、#9、TC-F-01、TC-F-09
 */
MiniFee_ReturnType MiniFee_Init(const uint16 *blockSizes, uint16 numBlk)
{
    FlashDrv_PropertyType prop;
    uint16 b;
    uint32 sumPages = 0u;
    uint16 maxPages = 0u;

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
    if ((numBlk == 0u) || (numBlk > MINIFEE_MAX_NUM_BLOCKS) || (blockSizes == NULL_PTR))
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

    /* 逐块校验并派生槽数：size 为 PAGE_SIZE 整数倍且 ≤ MAX_BLOCK_SIZE */
    for (b = 0u; b < numBlk; b++)
    {
        uint16 sz = blockSizes[b];
        uint16 pb;
        if ((sz == 0u) || ((sz % MINIFEE_PAGE_SIZE) != 0u) ||
            (sz > MINIFEE_MAX_BLOCK_SIZE))
        {
            return MINIFEE_ERR_PARAM;
        }
        pb = (uint16)(((uint32)sz / (uint32)MINIFEE_PAGE_SIZE) + 2u);
        blockSizeTbl[b] = sz;
        blockPages[b] = pb;
        sumPages += pb;
        if (pb > maxPages)
        {
            maxPages = pb;
        }
    }
    /* Model A 容量约束：单 active cluster 须容纳全部 live 槽 + 最大槽的新版本 */
    if ((sumPages + (uint32)maxPages) > (uint32)pagesPerCluster)
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
 * \brief 读块：无映射槽时按 corrupt 标志区分 ERR_CRC（扫描发现损坏槽）与 NOT_FOUND；
 *        命中映射则整槽读取校验后拷贝数据；墓碑槽（dataLen==0）返回 NOT_FOUND。
 * \req P0 #5、#10、TC-F-02、TC-F-05
 */
MiniFee_ReturnType MiniFee_ReadBlock(uint16 blockId, uint8 *dest, uint16 *dataLen)
{
    uint16 sq;
    uint16 dl;
    SlotStatus st;

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
        /* 扫描时发现损坏槽（已提交但 CRC 损坏）→ 上报 CRC 错误，便于 MiniNvm 区分 */
        if (blockMap[blockId].corrupt)
        {
            return MINIFEE_ERR_CRC;
        }
        return MINIFEE_ERR_NOT_FOUND;
    }
    st = readSlot(blockMap[blockId].pageAddr, blockId, &sq, &dl);
    if (st == SLOT_FLASH)
    {
        return MINIFEE_ERR_FLASH;
    }
    if (st != SLOT_OK)
    {
        return MINIFEE_ERR_CRC; /* 映射槽半写/损坏：按 CRC 错上报（防御路径） */
    }
    if (dl == 0u)
    {
        return MINIFEE_ERR_NOT_FOUND; /* 墓碑：块已擦除 */
    }
    (void)memcpy(dest, &workBuf[WBUF_OFF_DATA], dl);
    if (dataLen != NULL_PTR)
    {
        *dataLen = dl;
    }
    return MINIFEE_OK;
}

/**
 * \brief 写块：active 放不下先 gcAndSwitch；新槽 seq = 旧 seq+1（无旧槽为 0）；
 *        writeSlot 提交成功后更新 blockMap（不作废旧槽，靠 seq 竞争与 GC 回收；
 *        崩溃残留双已提交槽由恢复取最大 seq 化解）。
 * \req P0 #5、#6、#7、#9、TC-F-03、TC-F-07
 */
MiniFee_ReturnType MiniFee_WriteBlock(uint16 blockId, const uint8 *src, uint16 len)
{
    uint16 newSeq;
    uint32 newAddr;
    MiniFee_ReturnType r;

    if (!inited)
    {
        return MINIFEE_ERR_PARAM;
    }
    if ((blockId >= numBlocks) || (src == NULL_PTR) || (len > blockSizeTbl[blockId]))
    {
        return MINIFEE_ERR_PARAM;
    }

    /* 本次写入槽放不进当前 active cluster → 触发 GC */
    if ((writeCursor + blockPages[blockId]) > pagesPerCluster)
    {
        r = gcAndSwitch();
        if (r != MINIFEE_OK)
        {
            return r;
        }
    }

    newSeq = (blockMap[blockId].valid) ? (uint16)(blockMap[blockId].seq + 1u) : 0u;
    newAddr = pageAddr(activeCluster, writeCursor);

    /* 写新槽并提交（提交页最后写） */
    r = writeSlot(newAddr, blockId, newSeq, len, src);
    if (r != MINIFEE_OK)
    {
        return r;
    }

    blockMap[blockId].valid = TRUE;
    blockMap[blockId].corrupt = FALSE; /* 新写入清损坏标记 */
    blockMap[blockId].pageAddr = newAddr;
    blockMap[blockId].seq = newSeq;
    writeCursor = (uint16)(writeCursor + blockPages[blockId]);
    return MINIFEE_OK;
}

/**
 * \brief 擦除块：写墓碑槽（dataLen=0，seq+1）；无槽或已是墓碑时幂等返回 OK 不再写。
 * \req P0 #11、TC-F-06
 */
MiniFee_ReturnType MiniFee_EraseBlock(uint16 blockId)
{
    uint16 pb;
    uint16 newSeq;
    uint32 newAddr;
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
        return MINIFEE_OK; /* 幂等：无槽即视为已擦 */
    }
    pb = blockPages[blockId];

    /* 读映射槽提交页：已是墓碑（VALID 且 dataLen==0）→ 幂等 OK */
    if (FlashDrv_Read(blockMap[blockId].pageAddr + (uint32)(pb - 1u) * (uint32)MINIFEE_PAGE_SIZE,
                      MINIFEE_PAGE_SIZE, pageBuf) != FLASH_OK)
    {
        return MINIFEE_ERR_FLASH;
    }
    if ((getU16(&pageBuf[OFF_STATUS]) == MINIFEE_STATUS_VALID) &&
        (getU16(&pageBuf[OFF_DATALEN]) == 0u))
    {
        return MINIFEE_OK;
    }

    /* 墓碑槽放不进当前 active cluster → 触发 GC */
    if ((writeCursor + pb) > pagesPerCluster)
    {
        r = gcAndSwitch();
        if (r != MINIFEE_OK)
        {
            return r;
        }
    }

    newSeq = (uint16)(blockMap[blockId].seq + 1u);
    newAddr = pageAddr(activeCluster, writeCursor);
    r = writeSlot(newAddr, blockId, newSeq, 0u, NULL_PTR);
    if (r != MINIFEE_OK)
    {
        return r;
    }
    blockMap[blockId].valid = TRUE; /* 墓碑也是当前槽，保持 seq 链 */
    blockMap[blockId].corrupt = FALSE;
    blockMap[blockId].pageAddr = newAddr;
    blockMap[blockId].seq = newSeq;
    writeCursor = (uint16)(writeCursor + pb);
    return MINIFEE_OK;
}

/**
 * \brief 查询指定块槽数据区大小：返回 Init 时传入的该块 size。
 * \req TC-F-01
 */
uint16 MiniFee_GetBlockDataSize(uint16 blockId)
{
    return (blockId < numBlocks) ? blockSizeTbl[blockId] : 0u;
}

/**
 * \brief 查询物理写页大小（= MINIFEE_PAGE_SIZE，实际 Flash 最小编程字节数）。
 * \req TC-F-01
 */
uint16 MiniFee_GetWritePageSize(void)
{
    return MINIFEE_PAGE_SIZE;
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
