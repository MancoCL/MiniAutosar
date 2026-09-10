/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniFee.c
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniFee EEPROM 仿真接口，基于 Flash 驱动的异步状态机存储抽象层。
*                   按块号整块读写：块号由 MiniFee_BlockIdType 枚举定义，块大小由
*                   MiniFee_BlockConfig[] 逐块配置；追加式写入 + 新纪录遮蔽旧纪录；迁移利用
*                   块头 PrevOffset 反向扫描、按块号位图去重只迁移最新纪录，整簇擦除回收。
*                   无 Valid/Invalid 标志、无 CLOSED 二次编程，任何区域仅一次编程。
*
************************************************************************************************
*   限制         @: RH850 数据 Flash 每个程序单元在两次擦除间仅可编程一次（无 1→0 二次编程），
*                   块内不得包含事后单独编程的标志页；旧纪录仅能靠迁移时整簇擦除回收。
*
************************************************************************************************
*   修订历史:
*
*   版本       日期          编写人            CR#         描述
*   --------   -----------   ----------------   --------    -----------------------
*   V1.0       2026/09/10    CaoLiang           N/A         初版发布
*
************************************************************************************************
* END_FILE_HDR*/
#include "MiniFee.h"
#include "MiniFlsIf.h"

/**********************************************************************************************
* 内部宏
***********************************************************************************************/
/* FindAddr 类型 */
#define FIND_TYPE_READ                   (0u)
#define FIND_TYPE_WRITE                  (1u)

/* 迁移去重位图字节数：按逻辑块总数（MINIFEE_BLOCK_COUNT）计算，每块占 1 bit */
#define MINIFEE_MIGRATE_BITMAP_SIZE      (((MINIFEE_BLOCK_COUNT) + 7u) / 8u)

/**********************************************************************************************
* 内部类型
***********************************************************************************************/
/* 主状态机 */
typedef enum
{
    MINIFEE_STATE_IDLE = 0,
    MINIFEE_STATE_FLS_WAIT,                 /* 通用 Fls 异步等待 */
    MINIFEE_STATE_SCAN,                     /* 通用块头扫描 */

    /* ===== 读流程 ===== */
    MINIFEE_STATE_READ_FIND_ADDR,
    MINIFEE_STATE_READ_VERIFY_COPY,         /* 校验并拷贝整块数据 */
    MINIFEE_STATE_READ_DONE,

    /* ===== 写流程 ===== */
    MINIFEE_STATE_WRITE_FIND_ADDR,
    MINIFEE_STATE_WRITE_CHECK_CMP,          /* 与旧数据整块比对 */
    MINIFEE_STATE_WRITE_MAIN_LOOP_PROC,     /* 单次写入块头+数据 */
    MINIFEE_STATE_WRITE_DONE,

    /* ===== FindAddr / 簇选择子流程 ===== */
    MINIFEE_STATE_FIND_PROC,

    MINIFEE_STATE_ERROR
} MiniFee_StateType;

/* FindAddr 子状态 */
typedef enum
{
    FIND_SUB_START = 0,
    FIND_SUB_READ_CLUSTER_HDR_DONE,
    FIND_SUB_DECIDE,
    FIND_SUB_FRESH_ERASE,
    FIND_SUB_FRESH_ACTIVATE,
    FIND_SUB_METRICS_READ,
    FIND_SUB_METRICS_EVAL,
    FIND_SUB_METRICS_DONE,
    FIND_SUB_MIGRATE_PREPARE,
    FIND_SUB_MIGRATE_ERASE_DST,
    FIND_SUB_MIGRATE_READ,
    FIND_SUB_MIGRATE_EVAL,
    FIND_SUB_MIGRATE_DATA_READ,
    FIND_SUB_MIGRATE_WRITE,
    FIND_SUB_MIGRATE_ACTIVATE,
    FIND_SUB_MIGRATE_SWITCH,
    FIND_SUB_WRITE_HDR_READ,            /* 写簇头子流程：读当前簇 Generation */
    FIND_SUB_WRITE_HDR_WRITE,           /* 写簇头子流程：写目标簇头 */
    FIND_SUB_FRESH_ACTIVATE_DONE,       /* 首次激活：写簇头完成后的收尾 */
    FIND_SUB_DONE
} MiniFee_FindSubState;

/* 异步上下文（单一静态实例） */
typedef struct
{
    /* 任务描述 */
    uint32 blockNumber;            /* 目标块号（= 配置数组下标） */
    uint32 blockSize;              /* Flash 物理数据长度（按最小写单元对齐） */
    uint32 logicalBlockSize;       /* 上层逻辑数据长度 */
    uint8* buf;

    /* 状态机 */
    MiniFee_StateType    state;
    MiniFee_StatusType   status;
    MiniFee_StateType    flsWaitReturn;

    /* 共享缓冲 */
    uint8  clusterHdrBuf[MINIFEE_CLUSTER_HEADER_SIZE];
    uint8  blockDataBuf[MINIFEE_MAX_BLOCK_DATA_SIZE];
    uint8  writeBuf[MINIFEE_BLOCK_HEADER_SIZE + MINIFEE_MAX_BLOCK_DATA_SIZE];   /* 块头+数据合并写缓冲 */
    uint8  scanHdrBuf[MINIFEE_BLOCK_HEADER_SIZE];

    /* 活动簇信息（FindAddr 后得出） */
    uint32 activeClusterIndex;
    uint32 activeClusterStart;
    uint32 activeClusterLength;
    uint32 headerEnd;             /* 下一个块写入偏移（簇内） */
    uint32 freeSpace;
    uint32 lastRecordStart;       /* 最后一条有效纪录块头偏移（0=无纪录） */
    uint8  scanCorrupt;           /* 指标扫描时遇损坏纪录 */

    /* 写簇头子流程上下文（读取当前簇 Generation 自增后写目标簇头） */
    uint32 writeHdrClusterIndex;            /* 目标簇 */
    uint32 writeHdrGen;                     /* 计算出的 Generation */
    MiniFee_FindSubState writeHdrReturnState;  /* 写簇头完成后的子状态 */
    uint8  writeHdrBuf[MINIFEE_CLUSTER_HEADER_SIZE];   /* 目标簇头写缓冲（须持久：Fls_Write 异步持有源指针） */

    /* 通用块头扫描上下文 */
    uint32 curBlockNumber;        /* 扫描目标块号 */
    uint32 scanOff;
    uint8  scanPhase;             /* 0:读块头, 1:评估, 2:读数据, 3:数据读完成 */
    uint8  scanFindLatest;
    uint8  scanNeedData;
    uint8  scanFillBlank;
    uint8  scanTargetFound;
    uint32 scanTargetHdrOffset;
    uint32 scanTargetDataLength;
    MiniFee_StateType scanReturnState;

    /* FindAddr 子流程上下文 */
    MiniFee_FindSubState findSubState;
    MiniFee_StateType    findReturnState;
    uint8  findType;
    uint32 clusterIdx;
    uint32 bestGen;
    uint32 bestGenIdx;
    uint8  activeClusterHasData;
    uint8  findResultErr;

    /* 迁移上下文 */
    uint32 migrateDstIndex;
    uint32 migrateDstHdrEnd;
    uint32 migrateDstLastOffset;
    uint32 migrateDataLen;
    uint32 migratePrevOffset;
    uint8  migrateBitmap[MINIFEE_MIGRATE_BITMAP_SIZE];   /* 已迁移块号位图（去重） */

    /* 当前块头 */
    MiniFee_BlockHeaderType curHdr;
} MiniFee_ContextType;

/**********************************************************************************************
* 局部数据
***********************************************************************************************/
static MiniFee_ContextType MiniFee_Context;

/**********************************************************************************************
* 函数声明
***********************************************************************************************/
static void  MiniFee_ResetContext(void);
static uint8 MiniFee_StartFlsRead(uint32 offset, uint8* buf, uint32 len, MiniFee_StateType retState);
static uint8 MiniFee_StartFlsWrite(uint32 offset, const uint8* src, uint32 len, MiniFee_StateType retState);
static uint8 MiniFee_StartFlsErase(uint32 offset, uint32 len, MiniFee_StateType retState);
static void  HandleFlsWait(void);

/* 簇头序列化/反序列化 */
static void  PackClusterHdr(uint32 gen, uint8* buf);
static void  UnpackClusterHdr(const uint8* buf, uint32* magic, uint32* gen);
static uint8  IsClusterHeaderValid(const uint8* hdrBuf);
static uint8 StartWriteClusterHdr(uint32 clusterIndex, MiniFee_FindSubState returnState);

/* 块头序列化/反序列化 */
static void  PackBlockHdr(const MiniFee_BlockHeaderType* hdr, uint8* buf);
static void  UnpackBlockHdr(const uint8* buf, MiniFee_BlockHeaderType* hdr);
static uint8  MiniFee_Crc8(const uint8* data, uint32 length);
static uint8  CalcBlockChecksum(const MiniFee_BlockHeaderType* hdr);
static uint8  IsBlockHeaderValid(const uint8* hdrBuf);
static uint8  IsAllFF(const uint8* buf, uint32 len);

/* 通用块扫描 */
static void  HandleBlockScan(void);
static void  StartBlockScan(uint32 blockNumber, MiniFee_StateType returnState,
                            uint8 findLatest, uint8 needData, uint8 fillBlank);

/* 迁移去重位图辅助 */
static uint8  IsBitmapSet(const uint8* map, uint32 idx);
static void   SetBitmap(uint8* map, uint32 idx);

/* FindAddr / 簇选择 */
static void  StartFindAddr(MiniFee_StateType returnState, uint8 findType);
static void  ProcessFindAddr(void);

/* 辅助函数 */
static void  FinalizeOk(void);
static void  FinalizeErr(void);

/* 读流程 */
static void  HandleReadFindAddr(void);
static void  HandleReadVerifyCopy(void);

/* 写流程 */
static void  HandleWriteFindAddr(void);
static void  HandleWriteCheckCmp(void);
static void  HandleWriteMainLoopProc(void);

/**********************************************************************************************
* 局部函数
***********************************************************************************************/

/* 复位异步上下文到 IDLE */
static void MiniFee_ResetContext(void)
{
    CommF_DataSet(&MiniFee_Context, 0u, sizeof(MiniFee_ContextType));
    MiniFee_Context.state  = MINIFEE_STATE_IDLE;
    MiniFee_Context.status = MINIFEE_STATUS_IDLE;
}

/* 检查缓冲区是否全部为擦除态（0xFF） */
static uint8 IsAllFF(const uint8* buf, uint32 len)
{
    uint32 i;
    for (i = 0u; i < len; i++)
    {
        if (buf[i] != MINIFEE_ERASED_VALUE)
        {
            return 0u;
        }
    }
    return 1u;
}

/* 序列化簇头（8 字节，大端序）：Magic(4)+Generation(3)+CheckSum(1) */
static void PackClusterHdr(uint32 gen, uint8* buf)
{
    buf[0] = (uint8)((MINIFEE_CLUSTER_MAGIC >> 24) & 0xFFu);
    buf[1] = (uint8)((MINIFEE_CLUSTER_MAGIC >> 16) & 0xFFu);
    buf[2] = (uint8)((MINIFEE_CLUSTER_MAGIC >> 8) & 0xFFu);
    buf[3] = (uint8)(MINIFEE_CLUSTER_MAGIC & 0xFFu);
    buf[4] = (uint8)((gen >> 16) & 0xFFu);
    buf[5] = (uint8)((gen >> 8) & 0xFFu);
    buf[6] = (uint8)(gen & 0xFFu);
    buf[7] = MiniFee_Crc8(buf, 7u);
}

/* 反序列化簇头（无 Status） */
static void UnpackClusterHdr(const uint8* buf, uint32* magic, uint32* gen)
{
    *magic = ((uint32)buf[0] << 24) | ((uint32)buf[1] << 16)
           | ((uint32)buf[2] << 8)  | (uint32)buf[3];
    *gen   = ((uint32)buf[4] << 16) | ((uint32)buf[5] << 8) | (uint32)buf[6];
}

/* 从 8 字节簇头缓冲判定有效性（CRC-8 校验） */
static uint8 IsClusterHeaderValid(const uint8* hdrBuf)
{
    if (hdrBuf[7] != MiniFee_Crc8(hdrBuf, 7u))
    {
        return 0u;
    }
    return 1u;
}

/* 序列化块头为 8 字节（大端序），无标志页 */
static void PackBlockHdr(const MiniFee_BlockHeaderType* hdr, uint8* buf)
{
    buf[0] = (uint8)(hdr->BlockNumber & 0xFFu);
    buf[1] = (uint8)((hdr->Length >> 8) & 0xFFu);
    buf[2] = (uint8)(hdr->Length & 0xFFu);
    buf[3] = (uint8)((hdr->PrevOffset >> 24) & 0xFFu);
    buf[4] = (uint8)((hdr->PrevOffset >> 16) & 0xFFu);
    buf[5] = (uint8)((hdr->PrevOffset >> 8)  & 0xFFu);
    buf[6] = (uint8)(hdr->PrevOffset & 0xFFu);
    buf[7] = hdr->CheckSum;
}

/* 反序列化块头（8 字节，大端序） */
static void UnpackBlockHdr(const uint8* buf, MiniFee_BlockHeaderType* hdr)
{
    hdr->BlockNumber = buf[0];
    hdr->Length      = ((uint16)buf[1] << 8) | (uint16)buf[2];
    hdr->PrevOffset  = ((uint32)buf[3] << 24) | ((uint32)buf[4] << 16)
                     | ((uint32)buf[5] << 8)  | (uint32)buf[6];
    hdr->CheckSum    = buf[7];
}

/* CRC-8（多项式 0x07，初始值 0x00，不反相）：逐字节计算 */
static uint8 MiniFee_Crc8(const uint8* data, uint32 length)
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

/* 计算块头校验 = CRC-8（输入：BlockNumber(1) + Length(2 大端) + PrevOffset(4 大端)） */
static uint8 CalcBlockChecksum(const MiniFee_BlockHeaderType* hdr)
{
    uint8 buf[7];

    buf[0] = (uint8)(hdr->BlockNumber & 0xFFu);
    buf[1] = (uint8)((hdr->Length >> 8) & 0xFFu);
    buf[2] = (uint8)(hdr->Length & 0xFFu);
    buf[3] = (uint8)((hdr->PrevOffset >> 24) & 0xFFu);
    buf[4] = (uint8)((hdr->PrevOffset >> 16) & 0xFFu);
    buf[5] = (uint8)((hdr->PrevOffset >> 8)  & 0xFFu);
    buf[6] = (uint8)(hdr->PrevOffset & 0xFFu);

    return MiniFee_Crc8(buf, 7u);
}

/* 从 8 字节块头缓冲判定块有效性（仅校验和，无标志页） */
static uint8 IsBlockHeaderValid(const uint8* hdrBuf)
{
    MiniFee_BlockHeaderType hdr;

    UnpackBlockHdr(hdrBuf, &hdr);
    if (hdr.CheckSum != CalcBlockChecksum(&hdr))
    {
        return 0u;
    }
    return 1u;
}

/* 发起 MiniFlsIf_Read 并切换到 FLS_WAIT */
static uint8 MiniFee_StartFlsRead(uint32 offset, uint8* buf, uint32 len, MiniFee_StateType retState)
{
    uint8 r = MiniFlsIf_Read(offset, buf, len);
    if (r != E_OK)
    {
        MiniFee_Context.state = MINIFEE_STATE_ERROR;
        return E_NOT_OK;
    }
    MiniFee_Context.flsWaitReturn = retState;
    MiniFee_Context.state = MINIFEE_STATE_FLS_WAIT;
    return E_OK;
}

/* 发起 MiniFlsIf_Write 并切换到 FLS_WAIT */
static uint8 MiniFee_StartFlsWrite(uint32 offset, const uint8* src, uint32 len, MiniFee_StateType retState)
{
    uint8 r = MiniFlsIf_Write(offset, src, len);
    if (r != E_OK)
    {
        MiniFee_Context.state = MINIFEE_STATE_ERROR;
        return E_NOT_OK;
    }
    MiniFee_Context.flsWaitReturn = retState;
    MiniFee_Context.state = MINIFEE_STATE_FLS_WAIT;
    return E_OK;
}

/* 发起 MiniFlsIf_Erase 并切换到 FLS_WAIT */
static uint8 MiniFee_StartFlsErase(uint32 offset, uint32 len, MiniFee_StateType retState)
{
    uint8 r = MiniFlsIf_Erase(offset, len);
    if (r != E_OK)
    {
        MiniFee_Context.state = MINIFEE_STATE_ERROR;
        return E_NOT_OK;
    }
    MiniFee_Context.flsWaitReturn = retState;
    MiniFee_Context.state = MINIFEE_STATE_FLS_WAIT;
    return E_OK;
}

/* 发起簇头写入：先读取当前活动簇 Generation，自增后写目标簇头（异步子流程）。
 * Generation 不依赖外部参数：读取本簇 Generation +1；读取不到/失败则从 0 开始（写 1）。 */
static uint8 StartWriteClusterHdr(uint32 clusterIndex, MiniFee_FindSubState returnState)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    c->writeHdrClusterIndex = clusterIndex;
    c->writeHdrGen          = 0u;
    c->writeHdrReturnState  = returnState;

    /* 读取当前活动簇（源簇）簇头，获取其 Generation */
    if (MiniFee_StartFlsRead(MiniFee_ClusterConfig[c->activeClusterIndex].StartAddress - MINIFEE_FLS_BASE,
                             c->clusterHdrBuf, MINIFEE_CLUSTER_HEADER_SIZE,
                             MINIFEE_STATE_FIND_PROC) != E_OK)
    {
        return E_NOT_OK;
    }
    c->findSubState = FIND_SUB_WRITE_HDR_READ;
    return E_OK;
}

/* FLS_WAIT 处理：仅查询 MiniFlsIf 状态（与平台 Flash 驱动解耦） */
static void HandleFlsWait(void)
{
    MemIf_StatusType flsStatus;

    flsStatus = MiniFlsIf_GetStatus();

    if (flsStatus == MEMIF_BUSY)
    {
        return;
    }

    if (flsStatus != MEMIF_IDLE)
    {
        MiniFee_Context.state = MINIFEE_STATE_ERROR;
        return;
    }

    if (MiniFlsIf_GetJobResult() != MEMIF_JOB_OK)
    {
        MiniFee_Context.state = MINIFEE_STATE_ERROR;
        return;
    }

    MiniFee_Context.state = MiniFee_Context.flsWaitReturn;
}

static void FinalizeOk(void)
{
    MiniFee_Context.status = MINIFEE_STATUS_OK;
    MiniFee_Context.state  = MINIFEE_STATE_IDLE;
}

static void FinalizeErr(void)
{
    MiniFee_Context.status = MINIFEE_STATUS_NOT_OK;
    MiniFee_Context.state  = MINIFEE_STATE_IDLE;
}

/* 启动 FindAddr 子流程 */
static void StartFindAddr(MiniFee_StateType returnState, uint8 findType)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    c->findReturnState      = returnState;
    c->findType             = findType;
    c->findSubState         = FIND_SUB_START;
    c->clusterIdx           = 0u;
    c->bestGen              = 0u;
    c->bestGenIdx           = 0u;
    c->activeClusterHasData = 0u;
    c->findResultErr        = 0u;
    c->state                = MINIFEE_STATE_FIND_PROC;
}

/* 查询迁移位图 */
static uint8 IsBitmapSet(const uint8* map, uint32 idx)
{
    return (uint8)((map[idx / 8u] >> (idx % 8u)) & 0x01u);
}

/* 置位迁移位图 */
static void SetBitmap(uint8* map, uint32 idx)
{
    map[idx / 8u] |= (uint8)(1u << (idx % 8u));
}

/* 异步 FindAddr 子状态机：簇选择 + 指标扫描 + 迁移 */
static void ProcessFindAddr(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    switch (c->findSubState)
    {
        case FIND_SUB_START:
            if (c->clusterIdx >= MINIFEE_CLUSTER_COUNT)
            {
                c->findSubState = FIND_SUB_DECIDE;
                break;
            }
            if (MiniFee_StartFlsRead(MiniFee_ClusterConfig[c->clusterIdx].StartAddress - MINIFEE_FLS_BASE,
                                     c->clusterHdrBuf, MINIFEE_CLUSTER_HEADER_SIZE,
                                     MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_READ_CLUSTER_HDR_DONE;
            break;

        case FIND_SUB_READ_CLUSTER_HDR_DONE:
        {
            uint32 magic, gen;
            UnpackClusterHdr(c->clusterHdrBuf, &magic, &gen);
            /* 仅 CRC 校验通过且 Magic 匹配的簇头才视为有效（无 Status），损坏簇头不参与活动簇选择 */
            if ((IsClusterHeaderValid(c->clusterHdrBuf) != 0u) &&
                (magic == MINIFEE_CLUSTER_MAGIC))
            {
                c->activeClusterHasData = 1u;
                if (gen > c->bestGen)
                {
                    c->bestGen    = gen;
                    c->bestGenIdx = c->clusterIdx;
                }
            }
            c->clusterIdx++;
            c->findSubState = FIND_SUB_START;
            break;
        }

        case FIND_SUB_DECIDE:
            if (c->activeClusterHasData == 0u)
            {
                if (c->findType == FIND_TYPE_WRITE)
                {
                    c->findSubState = FIND_SUB_FRESH_ERASE;
                }
                else
                {
                    c->findResultErr = 1u;
                    c->findSubState  = FIND_SUB_DONE;
                }
                break;
            }
            c->activeClusterIndex  = c->bestGenIdx;
            c->activeClusterStart  = MiniFee_ClusterConfig[c->bestGenIdx].StartAddress;
            c->activeClusterLength = MiniFee_ClusterConfig[c->bestGenIdx].Length;
            if (c->findType == FIND_TYPE_WRITE)
            {
                c->scanOff          = MINIFEE_CLUSTER_HEADER_SIZE;
                c->headerEnd        = MINIFEE_CLUSTER_HEADER_SIZE;
                c->lastRecordStart  = 0u;
                c->scanCorrupt      = 0u;
                c->findSubState     = FIND_SUB_METRICS_READ;
            }
            else
            {
                c->findSubState = FIND_SUB_DONE;
            }
            break;

        case FIND_SUB_FRESH_ERASE:
            /* 仅在写入目标簇前擦除目标簇（Cluster 0），其余簇内容保留 */
            if (MiniFee_StartFlsErase(MiniFee_ClusterConfig[0u].StartAddress - MINIFEE_FLS_BASE,
                                      MiniFee_ClusterConfig[0u].Length,
                                      MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_FRESH_ACTIVATE;
            break;

        case FIND_SUB_FRESH_ACTIVATE:
            /* 首次激活簇 0：写簇头（Generation 自动自增，收尾在 FIND_SUB_FRESH_ACTIVATE_DONE） */
            if (StartWriteClusterHdr(0u, FIND_SUB_FRESH_ACTIVATE_DONE) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            break;

        case FIND_SUB_WRITE_HDR_READ:
        {
            uint32 magic;
            /* 读取当前活动簇 Generation（3 字节）；CRC 无效或 Magic 不匹配视为读取不到，从 0 开始 */
            magic = ((uint32)c->clusterHdrBuf[0] << 24) | ((uint32)c->clusterHdrBuf[1] << 16)
                  | ((uint32)c->clusterHdrBuf[2] << 8)  | (uint32)c->clusterHdrBuf[3];
            if ((IsClusterHeaderValid(c->clusterHdrBuf) != 0u) &&
                (magic == MINIFEE_CLUSTER_MAGIC))
            {
                c->writeHdrGen = ((uint32)c->clusterHdrBuf[4] << 16)
                               | ((uint32)c->clusterHdrBuf[5] << 8)
                               | (uint32)c->clusterHdrBuf[6];
            }
            else
            {
                c->writeHdrGen = 0u;
            }
            c->writeHdrGen++;   /* 自增 */
            /* 写缓冲须为持久缓冲：Fls_Write 异步持有源指针，写完成前不能使用栈上临时数组 */
            PackClusterHdr(c->writeHdrGen, c->writeHdrBuf);
            if (MiniFee_StartFlsWrite(MiniFee_ClusterConfig[c->writeHdrClusterIndex].StartAddress - MINIFEE_FLS_BASE,
                                      c->writeHdrBuf, MINIFEE_CLUSTER_HEADER_SIZE, MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_WRITE_HDR_WRITE;
            break;
        }

        case FIND_SUB_WRITE_HDR_WRITE:
            /* 写簇头完成，返回调用方设定的后续子状态 */
            c->findSubState = c->writeHdrReturnState;
            break;

        case FIND_SUB_FRESH_ACTIVATE_DONE:
        {
            uint32 needed = MINIFEE_BLOCK_TOTAL_OF(c->blockSize);
            c->findSubState = FIND_SUB_DONE;
            c->activeClusterIndex  = 0u;
            c->activeClusterStart  = MiniFee_ClusterConfig[0u].StartAddress;
            c->activeClusterLength = MiniFee_ClusterConfig[0u].Length;
            c->headerEnd       = MINIFEE_CLUSTER_HEADER_SIZE;
            c->lastRecordStart = 0u;
            c->freeSpace       = c->activeClusterLength - c->headerEnd;
            if (c->freeSpace < needed)
            {
                c->findResultErr = 1u;
            }
            break;
        }

        case FIND_SUB_METRICS_READ:
            if (c->scanOff + MINIFEE_BLOCK_HEADER_SIZE > c->activeClusterLength)
            {
                c->findSubState = FIND_SUB_METRICS_DONE;
                break;
            }
            if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE + c->scanOff,
                                     c->scanHdrBuf, MINIFEE_BLOCK_HEADER_SIZE,
                                     MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_METRICS_EVAL;
            break;

        case FIND_SUB_METRICS_EVAL:
            if (IsAllFF(c->scanHdrBuf, MINIFEE_BLOCK_HEADER_SIZE))
            {
                c->findSubState = FIND_SUB_METRICS_DONE;
                break;
            }
            if (IsBlockHeaderValid(c->scanHdrBuf) != 0u)
            {
                UnpackBlockHdr(c->scanHdrBuf, &c->curHdr);
                if ((c->curHdr.Length > MINIFEE_MAX_BLOCK_DATA_SIZE) ||
                    (MINIFEE_ALIGN_LEN(c->curHdr.Length) > (c->activeClusterLength - c->scanOff - MINIFEE_BLOCK_HEADER_SIZE)))
                {
                    /* 长度非法：视为损坏纪录，强制迁移回收 */
                    c->scanCorrupt = 1u;
                    c->findSubState = FIND_SUB_METRICS_DONE;
                    break;
                }
                c->lastRecordStart = c->scanOff;
                c->headerEnd       = c->scanOff + MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(c->curHdr.Length));
                c->scanOff         = c->headerEnd;
                c->findSubState    = FIND_SUB_METRICS_READ;
            }
            else
            {
                /* 校验和失败（损坏纪录）：强制迁移回收 */
                c->scanCorrupt = 1u;
                c->findSubState = FIND_SUB_METRICS_DONE;
            }
            break;

        case FIND_SUB_METRICS_DONE:
        {
            uint32 needed = MINIFEE_BLOCK_TOTAL_OF(c->blockSize);
            c->freeSpace = c->activeClusterLength - c->headerEnd;
            if ((c->scanCorrupt != 0u) || (c->freeSpace < needed))
            {
                c->findSubState = FIND_SUB_MIGRATE_PREPARE;
            }
            else
            {
                c->findSubState = FIND_SUB_DONE;
            }
            break;
        }

        case FIND_SUB_MIGRATE_PREPARE:
            c->migrateDstIndex     = (c->activeClusterIndex + 1u) % MINIFEE_CLUSTER_COUNT;
            c->migrateDstHdrEnd    = MINIFEE_CLUSTER_HEADER_SIZE;
            c->migrateDstLastOffset = 0u;
            c->migrateDataLen      = 0u;
            c->migratePrevOffset   = 0u;
            CommF_DataSet(c->migrateBitmap, 0u, sizeof(c->migrateBitmap));
            /* 从最后一条有效纪录反向扫描（最新→最旧），每个块号只迁移最新一份 */
            c->scanOff = c->lastRecordStart;
            c->findSubState = FIND_SUB_MIGRATE_ERASE_DST;
            break;

        case FIND_SUB_MIGRATE_ERASE_DST:
            c->findSubState = FIND_SUB_MIGRATE_READ;
            if (MiniFee_StartFlsErase(MiniFee_ClusterConfig[c->migrateDstIndex].StartAddress - MINIFEE_FLS_BASE,
                                      MiniFee_ClusterConfig[c->migrateDstIndex].Length,
                                      MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            break;

        case FIND_SUB_MIGRATE_READ:
            if ((c->scanOff == 0u) || (c->scanOff < MINIFEE_CLUSTER_HEADER_SIZE))
            {
                c->findSubState = FIND_SUB_MIGRATE_ACTIVATE;
                break;
            }
            if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE + c->scanOff,
                                     c->scanHdrBuf, MINIFEE_BLOCK_HEADER_SIZE,
                                     MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_MIGRATE_EVAL;
            break;

        case FIND_SUB_MIGRATE_EVAL:
            if (IsBlockHeaderValid(c->scanHdrBuf) == 0u)
            {
                /* 校验和失败：停止反向扫描，已迁移的纪录即最新集合 */
                c->findSubState = FIND_SUB_MIGRATE_ACTIVATE;
                break;
            }
            UnpackBlockHdr(c->scanHdrBuf, &c->curHdr);
            c->migratePrevOffset = c->curHdr.PrevOffset;
            if ((c->curHdr.Length > MINIFEE_MAX_BLOCK_DATA_SIZE) ||
                (MINIFEE_ALIGN_LEN(c->curHdr.Length) > (c->activeClusterLength - c->scanOff - MINIFEE_BLOCK_HEADER_SIZE)))
            {
                /* 长度非法：视为损坏，停止反向扫描 */
                c->findSubState = FIND_SUB_MIGRATE_ACTIVATE;
                break;
            }
            if (IsBitmapSet(c->migrateBitmap, c->curHdr.BlockNumber) == 0u)
            {
                SetBitmap(c->migrateBitmap, c->curHdr.BlockNumber);
                c->migrateDataLen = c->curHdr.Length;
                /* 目标簇内新纪录的 PrevOffset 指向已迁移的上一条 */
                c->curHdr.PrevOffset = c->migrateDstLastOffset;
                c->curHdr.CheckSum   = CalcBlockChecksum(&c->curHdr);
                PackBlockHdr(&c->curHdr, c->writeBuf);
                c->findSubState = FIND_SUB_MIGRATE_DATA_READ;
            }
            else
            {
                c->scanOff = c->migratePrevOffset;
                c->findSubState = FIND_SUB_MIGRATE_READ;
            }
            break;

        case FIND_SUB_MIGRATE_DATA_READ:
            if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE + c->scanOff
                                     + MINIFEE_BLOCK_HEADER_SIZE,
                                     &c->writeBuf[MINIFEE_BLOCK_HEADER_SIZE],
                                     MINIFEE_ALIGN_LEN(c->migrateDataLen),
                                     MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_MIGRATE_WRITE;
            break;

        case FIND_SUB_MIGRATE_WRITE:
            if (MiniFee_StartFlsWrite(MiniFee_ClusterConfig[c->migrateDstIndex].StartAddress - MINIFEE_FLS_BASE
                                      + c->migrateDstHdrEnd,
                                      c->writeBuf,
                                      MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(c->migrateDataLen)),
                                      MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->migrateDstLastOffset = c->migrateDstHdrEnd;
            c->migrateDstHdrEnd += MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(c->migrateDataLen));
            c->scanOff = c->migratePrevOffset;
            c->findSubState = FIND_SUB_MIGRATE_READ;
            break;

        case FIND_SUB_MIGRATE_ACTIVATE:
            /* 激活目标簇：写目标簇头（Generation 读取源簇自增）；源簇头保持原状，不二次改写 */
            if (StartWriteClusterHdr(c->migrateDstIndex, FIND_SUB_MIGRATE_SWITCH) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            break;

        case FIND_SUB_MIGRATE_SWITCH:
        {
            uint32 needed = MINIFEE_BLOCK_TOTAL_OF(c->blockSize);
            /* 老簇内容保留，不再擦除源簇；仅切换活动簇（源簇待下次作为迁移目标时再擦除） */
            c->activeClusterIndex  = c->migrateDstIndex;
            c->activeClusterStart  = MiniFee_ClusterConfig[c->migrateDstIndex].StartAddress;
            c->activeClusterLength = MiniFee_ClusterConfig[c->migrateDstIndex].Length;
            c->headerEnd       = c->migrateDstHdrEnd;
            c->lastRecordStart = c->migrateDstLastOffset;
            c->freeSpace       = c->activeClusterLength - c->headerEnd;
            c->findSubState    = FIND_SUB_DONE;
            /* 迁移后仍须容纳本次写入，否则报错 */
            if (c->freeSpace < needed)
            {
                c->findResultErr = 1u;
            }
            break;
        }

        case FIND_SUB_DONE:
        default:
            c->state = c->findReturnState;
            break;
    }
}

/* 通用块头扫描：定位 curBlockNumber 的最新有效纪录，随后读取其数据。
 * 各块长度不同，扫描步长由当前块头 Length 决定（块头+数据连续存放）。
 * 每次 FLS_WAIT 完成后经 MINIFEE_STATE_SCAN 重新进入。
 */
static void HandleBlockScan(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    MiniFee_BlockHeaderType hdr;
    uint8  run;

    run = 1u;
    while (run != 0u)
    {
        switch (c->scanPhase)
        {
            case 0u:
                if (c->scanOff + MINIFEE_BLOCK_HEADER_SIZE > c->activeClusterLength)
                {
                    if ((c->scanNeedData != 0u) && (c->scanTargetFound != 0u))
                    {
                        c->scanPhase = 2u;
                    }
                    else
                    {
                        run = 0u;
                    }
                    break;
                }
                if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE + c->scanOff,
                                         c->scanHdrBuf, MINIFEE_BLOCK_HEADER_SIZE,
                                         MINIFEE_STATE_SCAN) != E_OK)
                {
                    FinalizeErr();
                    return;
                }
                c->scanPhase = 1u;
                return;

            case 1u:
                if (IsAllFF(c->scanHdrBuf, MINIFEE_BLOCK_HEADER_SIZE))
                {
                    if ((c->scanNeedData != 0u) && (c->scanTargetFound != 0u))
                    {
                        c->scanPhase = 2u;
                    }
                    else
                    {
                        run = 0u;
                    }
                    break;
                }
                if (IsBlockHeaderValid(c->scanHdrBuf) != 0u)
                {
                    UnpackBlockHdr(c->scanHdrBuf, &hdr);
                    if ((hdr.Length > MINIFEE_MAX_BLOCK_DATA_SIZE) ||
                        (MINIFEE_ALIGN_LEN(hdr.Length) > (c->activeClusterLength - c->scanOff - MINIFEE_BLOCK_HEADER_SIZE)))
                    {
                        /* 长度非法：视为损坏纪录，结束扫描 */
                        run = 0u;
                        break;
                    }
                    if ((hdr.BlockNumber == c->curBlockNumber) && (c->scanFindLatest != 0u))
                    {
                        c->scanTargetHdrOffset  = c->scanOff;
                        c->scanTargetDataLength = hdr.Length;
                        c->scanTargetFound      = 1u;
                    }
                }
                else
                {
                    /* 校验和失败（损坏纪录）：结束扫描 */
                    run = 0u;
                    break;
                }
                c->scanOff += MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(hdr.Length));
                c->scanPhase = 0u;
                break;

            case 2u:
                if (c->scanTargetFound == 0u)
                {
                    run = 0u;
                    break;
                }
                if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE
                                         + c->scanTargetHdrOffset + MINIFEE_BLOCK_HEADER_SIZE,
                                         c->blockDataBuf, c->scanTargetDataLength,
                                         MINIFEE_STATE_SCAN) != E_OK)
                {
                    FinalizeErr();
                    return;
                }
                c->scanPhase = 3u;
                return;

            case 3u:
                run = 0u;
                break;

            default:
                FinalizeErr();
                return;
        }
    }

    /* 扫描结束 */
    if (c->scanTargetFound == 0u)
    {
        if (c->scanFillBlank != 0u)
        {
            CommF_DataSet(c->blockDataBuf, MINIFEE_ERASED_VALUE, c->blockSize);
        }
        c->scanTargetHdrOffset = 0xFFFFFFFFu;
    }
    c->state = c->scanReturnState;
}

/* 为指定块号建立通用块扫描 */
static void StartBlockScan(uint32 blockNumber, MiniFee_StateType returnState,
                           uint8 findLatest, uint8 needData, uint8 fillBlank)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    c->curBlockNumber       = blockNumber;
    c->scanReturnState      = returnState;
    c->scanOff              = MINIFEE_CLUSTER_HEADER_SIZE;
    c->scanPhase            = 0u;
    c->scanFindLatest       = findLatest;
    c->scanNeedData         = needData;
    c->scanFillBlank        = fillBlank;
    c->scanTargetFound      = 0u;
    c->scanTargetHdrOffset  = 0xFFFFFFFFu;
    c->scanTargetDataLength = 0u;
    c->state                = MINIFEE_STATE_SCAN;
}

/**********************************************************************************************
* 读流程状态处理
***********************************************************************************************/
static void HandleReadFindAddr(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->findResultErr != 0u)
    {
        FinalizeErr();
        return;
    }
    StartBlockScan(c->blockNumber, MINIFEE_STATE_READ_VERIFY_COPY, 1u, 1u, 0u);
}

static void HandleReadVerifyCopy(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->scanTargetFound == 0u)
    {
        FinalizeErr();
        return;
    }
    /* 只返回逻辑数据，物理对齐填充由 MiniFee 隐藏。 */
    CommF_DataCopy(c->buf, c->blockDataBuf, c->logicalBlockSize);
    c->state = MINIFEE_STATE_READ_DONE;
}

/**********************************************************************************************
* 写流程状态处理
***********************************************************************************************/
static void HandleWriteFindAddr(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->findResultErr != 0u)
    {
        FinalizeErr();
        return;
    }
    StartBlockScan(c->blockNumber, MINIFEE_STATE_WRITE_CHECK_CMP, 1u, 1u, 1u);
}

static void HandleWriteCheckCmp(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    uint32 i;

    /* 整块比对：与最新纪录完全一致则跳过写入 */
    for (i = 0u; i < c->logicalBlockSize; i++)
    {
        if (c->blockDataBuf[i] != c->buf[i])
        {
            c->state = MINIFEE_STATE_WRITE_MAIN_LOOP_PROC;
            return;
        }
    }
    c->state = MINIFEE_STATE_WRITE_DONE;
}

static void HandleWriteMainLoopProc(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    uint32 writeOffset;

    /* 组装块头（PrevOffset 指向上一条纪录）与数据，单次写入（无标志页）
     * Length 记录逻辑数据长度，数据区按物理对齐长度写入（真实数据 + 0 填充） */
    c->curHdr.BlockNumber = c->blockNumber;
    c->curHdr.Length      = c->logicalBlockSize;
    c->curHdr.PrevOffset  = c->lastRecordStart;
    c->curHdr.CheckSum    = CalcBlockChecksum(&c->curHdr);
    PackBlockHdr(&c->curHdr, c->writeBuf);
    CommF_DataCopy(&c->writeBuf[MINIFEE_BLOCK_HEADER_SIZE], c->buf, c->logicalBlockSize);
    CommF_DataSet(&c->writeBuf[MINIFEE_BLOCK_HEADER_SIZE + c->logicalBlockSize],
                  0u,
                  c->blockSize - c->logicalBlockSize);

    writeOffset = c->headerEnd;
    c->lastRecordStart = writeOffset;
    c->headerEnd += MINIFEE_BLOCK_TOTAL_OF(c->blockSize);

    if (MiniFee_StartFlsWrite(c->activeClusterStart - MINIFEE_FLS_BASE + writeOffset,
                              c->writeBuf, MINIFEE_BLOCK_TOTAL_OF(c->blockSize),
                              MINIFEE_STATE_WRITE_DONE) != E_OK)
    {
        FinalizeErr();
        return;
    }
}

/**********************************************************************************************
* 对外接口
***********************************************************************************************/

void MiniFee_Init(void)
{
    (void)MiniFlsIf_Init();
    MiniFee_ResetContext();
}

uint8 MiniFee_DeInit(void)
{
    (void)MiniFlsIf_DeInit();
    MiniFee_ResetContext();
    return E_OK;
}

uint8 MiniFee_Read(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    if (c->status == MINIFEE_STATUS_BUSY)
    {
        return E_NOT_OK;
    }
    if (buf == NULL_PTR)
    {
        return E_NOT_OK;
    }
    if (blockNumber >= MINIFEE_BLOCK_COUNT)
    {
        return E_NOT_OK;
    }
    if (blockNumber != MiniFee_BlockConfig[blockNumber].BlockNumber)
    {
        return E_NOT_OK;
    }
    if (size != MiniFee_BlockConfig[blockNumber].Length)
    {
        return E_NOT_OK;
    }
    if (size == 0u)
    {
        return E_OK;
    }

    MiniFee_ResetContext();
    c->blockNumber  = blockNumber;
    c->logicalBlockSize = size;
    c->blockSize    = (size + (MINIFEE_VIRTUALPAGE_SIZE - 1u)) &
                      ~(MINIFEE_VIRTUALPAGE_SIZE - 1u);
    c->buf          = buf;
    c->status       = MINIFEE_STATUS_BUSY;
    c->state        = MINIFEE_STATE_READ_FIND_ADDR;
    StartFindAddr(MINIFEE_STATE_READ_FIND_ADDR, FIND_TYPE_READ);
    return E_OK;
}

uint8 MiniFee_Write(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    if (c->status == MINIFEE_STATUS_BUSY)
    {
        return E_NOT_OK;
    }
    if (buf == NULL_PTR)
    {
        return E_NOT_OK;
    }
    if (blockNumber >= MINIFEE_BLOCK_COUNT)
    {
        return E_NOT_OK;
    }
    if (blockNumber != MiniFee_BlockConfig[blockNumber].BlockNumber)
    {
        return E_NOT_OK;
    }
    if (size != MiniFee_BlockConfig[blockNumber].Length)
    {
        return E_NOT_OK;
    }
    if (size == 0u)
    {
        return E_OK;
    }

    MiniFee_ResetContext();
    c->blockNumber  = blockNumber;
    c->logicalBlockSize = size;
    c->blockSize    = (size + (MINIFEE_VIRTUALPAGE_SIZE - 1u)) &
                      ~(MINIFEE_VIRTUALPAGE_SIZE - 1u);
    c->buf          = buf;
    c->status       = MINIFEE_STATUS_BUSY;
    c->state        = MINIFEE_STATE_WRITE_FIND_ADDR;
    StartFindAddr(MINIFEE_STATE_WRITE_FIND_ADDR, FIND_TYPE_WRITE);
    return E_OK;
}

uint8 MiniFee_Cancel(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->status != MINIFEE_STATUS_BUSY)
    {
        return E_NOT_OK;
    }
    /* 取消：复位到 IDLE 并置 NOT_OK 状态 */
    c->status = MINIFEE_STATUS_NOT_OK;
    c->state  = MINIFEE_STATE_IDLE;
    return E_OK;
}

void MiniFee_MainFunction(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    if (c->state == MINIFEE_STATE_IDLE)
    {
        return;
    }

    switch (c->state)
    {
        case MINIFEE_STATE_FLS_WAIT:
            HandleFlsWait();
            break;

        case MINIFEE_STATE_FIND_PROC:
            ProcessFindAddr();
            break;

        case MINIFEE_STATE_SCAN:
            HandleBlockScan();
            break;

        /* ----- 读 ----- */
        case MINIFEE_STATE_READ_FIND_ADDR:
            HandleReadFindAddr();
            break;
        case MINIFEE_STATE_READ_VERIFY_COPY:
            HandleReadVerifyCopy();
            break;
        case MINIFEE_STATE_READ_DONE:
            FinalizeOk();
            break;

        /* ----- 写 ----- */
        case MINIFEE_STATE_WRITE_FIND_ADDR:
            HandleWriteFindAddr();
            break;
        case MINIFEE_STATE_WRITE_CHECK_CMP:
            HandleWriteCheckCmp();
            break;
        case MINIFEE_STATE_WRITE_MAIN_LOOP_PROC:
            HandleWriteMainLoopProc();
            break;
        case MINIFEE_STATE_WRITE_DONE:
            FinalizeOk();
            break;

        case MINIFEE_STATE_ERROR:
            FinalizeErr();
            break;

        default:
            FinalizeErr();
            break;
    }
}

MiniFee_StatusType MiniFee_GetStatus(void)
{
    return MiniFee_Context.status;
}
