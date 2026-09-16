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
*   作者         @: Manco
************************************************************************************************
*   描述         @: MiniFee EEPROM 仿真接口，基于 Flash 驱动的异步状态机存储抽象层。
*                   按块号整块读写：块号由 MiniFee_BlockIdType 枚举定义，块大小由
 *                   MiniFee_BlockConfig[] 逐块配置；追加式写入 + RAM 块目录定位最新纪录；迁移
 *                   按块目录逐块拷贝每块最新纪录到备用簇，整簇擦除回收。
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
*   V1.0       2026/09/10    Manco              N/A         初版发布
*   V1.1       2026/09/16    Manco              N/A         重构：RAM 块目录、一次性读写、迁移改写
*
************************************************************************************************
* END_FILE_HDR*/
#include "MiniFee.h"
#include "MiniFlsIf.h"

/**********************************************************************************************
* 模块实现总览（阅读顺序建议）
* --------------------------------------------------------------------------------------------
* 一、物理布局
*   每个 Cluster（簇） = [8B 簇头][记录1][记录2]...[记录N][0xFF 空白...]
*   簇头：Magic(4) + Generation(3, 大端) + CRC-8(1)；活动簇 = Magic 正确且 Generation 最大的簇。
 *   记录：8B 块头 + align(Length) 数据；块头 = BlockNumber(3) + Length(4) + CRC-8(1)。
 *   “某块最新记录”由 RAM 块目录（dirOffset[]）维护。
*
* 二、写入模型（追加式）
*   同一块每次写入都追加到活动簇 headerEnd 处，旧记录不删除；读取时扫描取该块“最后一条”有效记录。
*   这样避免原地改写（Flash 擦除前只能编程一次）。旧记录只有在下一次整簇迁移擦除时才被回收。
*
 * 三、块目录（RAM 缓存，加速读/写）
 *   FindAddr 单遍扫描活动簇块头，建立“块号 -> 最新记录块头偏移”目录（`dirOffset`/`dirValid`），
 *   并缓存活动簇 Generation（`activeClusterGen`）供写簇头直接取用（免回读）。
 *   冷启动首次操作构建一次；之后单块读只需 1 次数据读、写入 0 读（直接追加），不再重复扫描记录头。
 *   追加写/迁移/首次激活时同步更新目录；目录随作业保留，仅在 Init/DeInit 或重建时失效。
 *
 * 四、读流程（MiniFee_Read / MiniFee_ReadAll -> MiniFee_MainFunction 驱动）
 *   单块：READ_BY_DIR(按目录读目标块最新数据) -> READ_VERIFY_COPY(拷贝逻辑长度到用户 buf) -> READ_DONE
 *   全量：READALL_FILL(按目录逐块读每块最新数据并回调) -> READ_DONE
 *
 * 五、写流程（MiniFee_Write / MiniFee_WriteAll -> MiniFee_MainFunction 驱动）
 *   单块：WRITE_MAIN_LOOP_PROC(组装块头+数据, 单次追加写入) -> WRITE_DONE
 *   全量：WRITEALL_APPEND(按块号连续追加脏块记录) -> WRITE_DONE
 *   写入不做内容去重（脏块过滤由上层 MiniNvm 完成）；
 *   目录未就绪或空间不足时，先由 FindAddr 完成选簇/迁移/构建目录；若本次写入触发迁移，
 *   被覆盖的块已在迁移阶段按新数据写过一次，写入阶段被跳过，直接收尾。
 *
 * 六、FindAddr 子状态机（MINIFEE_STATE_FIND_PROC，可被 FLS_WAIT 反复打断）
 *   逐簇读簇头选活动簇（并缓存其 Generation）；无活动簇时：写请求首次激活簇 0，读请求建空目录返回全零。
 *   有活动簇时扫描已用空间：合法记录登记进块目录并推进 headerEnd；
 *   目录已对所选活动簇有效时直接复用、不再重扫（写请求空间不足则直接进迁移）；
 *   遇损坏记录或剩余空间不足（仅写请求）-> 迁移：擦目标簇 -> 单遍写目标簇（本次作业要覆盖的块直接写
 *   新数据，其余有记录的块搬运旧数据）-> 写目标簇头(Generation+1) -> 切换活动簇。
 *
 * 七、关键约束
 *   - 任何区域两次擦除之间只编程一次，无 Valid/Invalid 标志页，不进行 1→0 二次编程；
 *   - 写缓冲必须是上下文成员（Fls_Write 异步持有源指针），禁止用栈上临时数组；
 *   - 迁移只在“写前”擦除目标簇，源簇保留以便断电回退，待下次作为目标时再擦除。
 ***********************************************************************************************/

/**********************************************************************************************
* 内部宏
***********************************************************************************************/
/* FindAddr 类型 */
#define FIND_TYPE_READ                   (0u)
#define FIND_TYPE_WRITE                  (1u)

/**********************************************************************************************
* 内部类型
***********************************************************************************************/
/* 主状态机：MiniFee_MainFunction 每次根据当前 state 调用对应的处理函数，
 * 处理函数内部可能发起一次 MiniFlsIf 异步操作并把 state 切到 FLS_WAIT；
 * FLS_WAIT 完成后经 flsWaitReturn 回到原流程继续推进。 */
typedef enum
{
    MINIFEE_STATE_IDLE = 0,                 /* 空闲：MainFunction 直接返回 */
    MINIFEE_STATE_FLS_WAIT,                 /* 通用 Fls 异步等待 */

    /* ===== 读流程 ===== */
    MINIFEE_STATE_READ_BY_DIR,              /* 按块目录直接读单块最新数据 */
    MINIFEE_STATE_READ_VERIFY_COPY,         /* 校验并拷贝逻辑数据 */
    MINIFEE_STATE_READ_DONE,
    MINIFEE_STATE_READALL_FILL,             /* 一次性读：按目录逐块读最新数据并回调 */

    /* ===== 写流程 ===== */
    MINIFEE_STATE_WRITE_MAIN_LOOP_PROC,     /* 组装并单次追加写入块头+数据 */
    MINIFEE_STATE_WRITE_DONE,
    MINIFEE_STATE_WRITEALL_APPEND,          /* 一次性写：选簇后连续追加脏块记录 */

    /* ===== FindAddr / 簇选择与块目录构建子流程 ===== */
    MINIFEE_STATE_FIND_PROC,

    MINIFEE_STATE_ERROR
} MiniFee_StateType;

/* FindAddr 子状态：ProcessFindAddr() 按该状态逐拍推进。
 * 每发起一次 Flash 读/写/擦除都会切到主状态 FLS_WAIT，完成后重新进入 FIND_PROC，
 * 因此子状态机必须自己记住“下一步该做什么”，不能依赖函数调用栈。 */
typedef enum
{
    FIND_SUB_START = 0,                 /* 逐簇读取簇头 */
    FIND_SUB_READ_CLUSTER_HDR_DONE,     /* 解析刚读到的簇头，选 Generation 最大者为活动簇 */
    FIND_SUB_DECIDE,                    /* 决策：有无活动簇、读还是写 */
    FIND_SUB_FRESH_ERASE,               /* 首次激活：擦除簇 0 */
    FIND_SUB_FRESH_ACTIVATE,            /* 首次激活：写簇 0 簇头 */
    FIND_SUB_METRICS_READ,              /* 写前扫描：逐条读块头 */
    FIND_SUB_METRICS_EVAL,              /* 扫描：评估块头、登记块目录并推进 headerEnd */
    FIND_SUB_METRICS_DONE,              /* 写前扫描：汇总空间/损坏情况，决定是否迁移 */
    FIND_SUB_MIGRATE_PREPARE,           /* 迁移准备：算目标簇、置块游标 */
    FIND_SUB_MIGRATE_ERASE_DST,         /* 擦除目标簇 */
    FIND_SUB_MIGRATE_READ,              /* 取下一个目标簇待写块：本次要写的块直接组装新数据，否则读源数据 */
    FIND_SUB_MIGRATE_WRITE,             /* 写目标簇新记录并更新目录 */
    FIND_SUB_MIGRATE_ACTIVATE,          /* 写目标簇簇头（Generation = activeClusterGen + 1） */
    FIND_SUB_MIGRATE_SWITCH,            /* 切换活动簇到目标簇（老簇保留） */
    FIND_SUB_WRITE_HDR_WRITE,           /* 写簇头子流程：写目标簇头（Generation 取自缓存） */
    FIND_SUB_FRESH_ACTIVATE_DONE,       /* 首次激活：写簇头完成后的收尾 */
    FIND_SUB_DONE                       /* 子流程结束，回到 findReturnState */
} MiniFee_FindSubState;

/* 异步上下文（单一静态实例） */
typedef struct
{
    /* 任务描述 */
    uint32 blockNumber;            /* 目标块号（= 配置数组下标） */
    uint32 blockSize;              /* Flash 物理数据长度（按最小写单元对齐） */
    uint32 logicalBlockSize;       /* 上层逻辑数据长度 */
    uint8* buf;

    /* 一次性读（MiniFee_ReadAll）交付回调 */
    MiniFee_ReadAllCallbackType readAllCallback;

    /* 一次性写（MiniFee_WriteAll）取数回调与进度 */
    MiniFee_WriteAllDataCallbackType writeAllCallback;
    uint32 bulkWriteNeeded;        /* 本次一次性写全部脏块的预估总占用（块头+对齐数据） */
    uint32 writeAllBlock;          /* 一次性写：当前待处理块号游标（0..MINIFEE_BLOCK_COUNT） */
    uint32 readAllBlock;           /* 一次性读：当前待处理块号游标（0..MINIFEE_BLOCK_COUNT） */

    /* 状态机 */
    MiniFee_StateType    state;
    MiniFee_StatusType   status;
    MiniFee_StateType    flsWaitReturn;

    /* 共享缓冲 */
    uint8  clusterHdrBuf[MINIFEE_CLUSTER_HEADER_SIZE];
    /* 作业数据缓冲：读作业只用 blockDataBuf、写作业（含迁移）只用 writeBuf；
     * 单上下文 Busy 互斥使两类作业不并发，故共用同一块 RAM（省 880B）。禁止跨类型混用。 */
    union
    {
        uint8  blockDataBuf[MINIFEE_MAX_BLOCK_DATA_SIZE];                        /* 读：当前块数据 */
        uint8  writeBuf[MINIFEE_BLOCK_HEADER_SIZE + MINIFEE_MAX_BLOCK_DATA_SIZE]; /* 写：块头+数据 */
    } jobBuf;
    uint8  scanHdrBuf[MINIFEE_BLOCK_HEADER_SIZE];

    /* 活动簇信息 + 块目录（FindAddr 后得出；跨作业保留，作为读/写快路径依据） */
    uint32 activeClusterIndex;
    uint32 activeClusterStart;
    uint32 activeClusterLength;
    uint32 activeClusterGen;      /* 活动簇 Generation 缓存（目录构建时取得，写簇头直接 +1，免去回读） */
    uint32 headerEnd;             /* 下一个块写入偏移（簇内） */
    uint8  scanCorrupt;           /* 扫描时遇损坏纪录 */
    uint8  dirValid;              /* 块目录是否对当前活动簇有效 */
    uint32 dirOffset[MINIFEE_BLOCK_COUNT];  /* 每块最新记录块头偏移；0xFFFFFFFF=无记录 */

    /* 写簇头子流程上下文（读取当前簇 Generation 自增后写目标簇头） */
    uint32 writeHdrClusterIndex;            /* 目标簇 */
    uint32 writeHdrGen;                     /* 计算出的 Generation */
    MiniFee_FindSubState writeHdrReturnState;  /* 写簇头完成后的子状态 */
    uint8  writeHdrBuf[MINIFEE_CLUSTER_HEADER_SIZE];   /* 目标簇头写缓冲（须持久：Fls_Write 异步持有源指针） */

    /* 扫描/遍历游标 */
    uint32 scanOff;               /* 目录构建扫描偏移 */
    uint8  scanPhase;             /* 通用阶段标记（目录构建/一次性读交付） */
    uint8  scanTargetFound;       /* 单块读是否命中（供 VerifyCopy 判断） */

    /* FindAddr 子流程上下文 */
    MiniFee_FindSubState findSubState;
    MiniFee_StateType    findReturnState;
    uint8  findType;
    uint32 clusterIdx;
    uint32 bestGen;
    uint32 bestGenIdx;
    uint8  activeClusterHasData;
    uint8  findResultErr;

    /* 迁移上下文（按块目录逐块迁移，不再依赖块头链） */
    uint32 migrateDstIndex;
    uint32 migrateDstHdrEnd;
    uint32 migrateBlock;          /* 当前待迁移块号游标（0..MINIFEE_BLOCK_COUNT） */
    uint32 migrateDataLen;        /* 当前迁移记录的逻辑长度 */

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
static void  MiniFee_ResetJob(void);
static void  MiniFee_ClearDirMap(void);
static uint8 MiniFee_StartFlsRead(uint32 offset, uint8* buf, uint32 len, MiniFee_StateType retState);
static uint8 MiniFee_StartFlsWrite(uint32 offset, const uint8* src, uint32 len, MiniFee_StateType retState);
static uint8 MiniFee_StartFlsErase(uint32 offset, uint32 len, MiniFee_StateType retState);
static void  HandleFlsWait(void);

/* 簇头序列化/反序列化 */
static void  PackClusterHdr(uint32 gen, uint8* buf);
static void  UnpackClusterHdr(const uint8* buf, uint32* magic, uint32* gen);
static uint8  IsClusterHeaderValid(const uint8* hdrBuf);
static uint32 ClusterHdrGen(const uint8* hdrBuf);
static uint8 StartWriteClusterHdr(uint32 clusterIndex, MiniFee_FindSubState returnState);

/* 块头序列化/反序列化 */
static void  PackBlockHdr(const MiniFee_BlockHeaderType* hdr, uint8* buf);
static void  UnpackBlockHdr(const uint8* buf, MiniFee_BlockHeaderType* hdr);
static uint8  MiniFee_Crc8(const uint8* data, uint32 length);
static uint8  CalcBlockChecksum(const MiniFee_BlockHeaderType* hdr);
static uint8  IsBlockHeaderValid(const uint8* hdrBuf);
static uint8  IsAllFF(const uint8* buf, uint32 len);

/* FindAddr 空间判断 */
static uint32 MiniFee_NeededSpace(void);

/* FindAddr / 簇选择 */
static void  StartFindAddr(MiniFee_StateType returnState, uint8 findType);
static void  ProcessFindAddr(void);

/* 辅助函数 */
static void  FinalizeOk(void);
static void  FinalizeErr(void);

/* 读流程 */
static void  HandleReadByDir(void);
static void  HandleReadVerifyCopy(void);
static void  HandleReadAllFill(void);

/* 写流程 */
static void  HandleWriteMainLoopProc(void);
static void  HandleWriteAllAppend(void);
static void  PackNewRecord(uint32 block, uint32 len, const uint8* data);
static const uint8* GetTargetData(uint32 block);

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

/* 复位“作业”相关字段，但保留块目录与活动簇/尾游标（供后续读/写走快路径）。
 * 块目录（dirOffset/dirValid/activeClusterx/headerEnd）跨作业保留，
 * 仅在 Init/DeInit 或重建目录时失效。 */
static void MiniFee_ResetJob(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    c->blockNumber          = 0u;
    c->blockSize            = 0u;
    c->logicalBlockSize     = 0u;
    c->buf                  = NULL_PTR;
    c->readAllCallback      = NULL_PTR;
    c->writeAllCallback     = NULL_PTR;
    c->bulkWriteNeeded      = 0u;
    c->writeAllBlock        = 0u;
    c->readAllBlock         = 0u;
    c->flsWaitReturn        = MINIFEE_STATE_IDLE;
    c->scanOff              = 0u;
    c->scanPhase            = 0u;
    c->scanTargetFound      = 0u;
    c->scanCorrupt          = 0u;
    c->writeHdrClusterIndex = 0u;
    c->writeHdrGen          = 0u;
    c->writeHdrReturnState  = FIND_SUB_START;
    c->findSubState         = FIND_SUB_START;
    c->findReturnState      = MINIFEE_STATE_IDLE;
    c->findType             = 0u;
    c->clusterIdx           = 0u;
    c->bestGen              = 0u;
    c->bestGenIdx           = 0u;
    c->activeClusterHasData = 0u;
    c->findResultErr        = 0u;
    c->migrateDstIndex      = 0u;
    c->migrateDstHdrEnd     = 0u;
    c->migrateBlock         = 0u;
    c->migrateDataLen       = 0u;
    c->curHdr.BlockNumber   = 0u;
    c->curHdr.Length        = 0u;
    c->curHdr.CheckSum      = 0u;
    c->state                = MINIFEE_STATE_IDLE;
    c->status               = MINIFEE_STATUS_IDLE;
}

/* 清空块目录映射（每次重建目录前调用）：0xFFFFFFFF 表示该块当前簇内无记录 */
static void MiniFee_ClearDirMap(void)
{
    uint32 i;
    for (i = 0u; i < MINIFEE_BLOCK_COUNT; i++)
    {
        MiniFee_Context.dirOffset[i] = 0xFFFFFFFFu;
    }
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

/* 解析簇头：CRC 与 Magic 均有效时返回 Generation（0..0xFFFFFF），否则返回 0xFFFFFFFF（无效） */
static uint32 ClusterHdrGen(const uint8* hdrBuf)
{
    uint32 magic;
    uint32 gen;

    if (IsClusterHeaderValid(hdrBuf) == 0u)
    {
        return 0xFFFFFFFFu;
    }
    UnpackClusterHdr(hdrBuf, &magic, &gen);
    if (magic != MINIFEE_CLUSTER_MAGIC)
    {
        return 0xFFFFFFFFu;
    }
    return gen;
}

/* 序列化块头为 8 字节（大端序）：BlockNumber(3)+Length(4)+CheckSum(1)，无标志页 */
static void PackBlockHdr(const MiniFee_BlockHeaderType* hdr, uint8* buf)
{
    buf[0] = (uint8)((hdr->BlockNumber >> 16) & 0xFFu);
    buf[1] = (uint8)((hdr->BlockNumber >> 8)  & 0xFFu);
    buf[2] = (uint8)(hdr->BlockNumber & 0xFFu);
    buf[3] = (uint8)((hdr->Length >> 24) & 0xFFu);
    buf[4] = (uint8)((hdr->Length >> 16) & 0xFFu);
    buf[5] = (uint8)((hdr->Length >> 8)  & 0xFFu);
    buf[6] = (uint8)(hdr->Length & 0xFFu);
    buf[7] = hdr->CheckSum;
}

/* 反序列化块头（8 字节，大端序）：BlockNumber(3)+Length(4)+CheckSum(1) */
static void UnpackBlockHdr(const uint8* buf, MiniFee_BlockHeaderType* hdr)
{
    hdr->BlockNumber = ((uint32)buf[0] << 16) | ((uint32)buf[1] << 8) | (uint32)buf[2];
    hdr->Length      = ((uint32)buf[3] << 24) | ((uint32)buf[4] << 16)
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

/* 计算块头校验 = CRC-8（输入：BlockNumber(3 大端) + Length(4 大端)） */
static uint8 CalcBlockChecksum(const MiniFee_BlockHeaderType* hdr)
{
    uint8 buf[7];

    buf[0] = (uint8)((hdr->BlockNumber >> 16) & 0xFFu);
    buf[1] = (uint8)((hdr->BlockNumber >> 8)  & 0xFFu);
    buf[2] = (uint8)(hdr->BlockNumber & 0xFFu);
    buf[3] = (uint8)((hdr->Length >> 24) & 0xFFu);
    buf[4] = (uint8)((hdr->Length >> 16) & 0xFFu);
    buf[5] = (uint8)((hdr->Length >> 8)  & 0xFFu);
    buf[6] = (uint8)(hdr->Length & 0xFFu);

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

/* 发起簇头写入：Generation 直接取活动簇缓存值 activeClusterGen +1（不再回读源簇头），
 * 组包到持久缓冲 writeHdrBuf 后异步写入目标簇头。 */
static uint8 StartWriteClusterHdr(uint32 clusterIndex, MiniFee_FindSubState returnState)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    c->writeHdrClusterIndex = clusterIndex;
    c->writeHdrReturnState  = returnState;
    /* 无有效活动簇（首次激活/空白）时 activeClusterGen 为 0xFFFFFFFF，自增后写 1 */
    c->writeHdrGen = (c->activeClusterGen == 0xFFFFFFFFu) ? 0u : c->activeClusterGen;
    c->writeHdrGen++;   /* 自增 */

    /* 持久缓冲：Fls_Write 异步持有源指针，写完成前不能使用栈上临时数组 */
    PackClusterHdr(c->writeHdrGen, c->writeHdrBuf);
    if (MiniFee_StartFlsWrite(MiniFee_ClusterConfig[c->writeHdrClusterIndex].StartAddress - MINIFEE_FLS_BASE,
                              c->writeHdrBuf, MINIFEE_CLUSTER_HEADER_SIZE, MINIFEE_STATE_FIND_PROC) != E_OK)
    {
        return E_NOT_OK;
    }
    c->findSubState = FIND_SUB_WRITE_HDR_WRITE;
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

/* 任务成功收尾：结果置 OK，状态机回 IDLE，等待上层查询或下一次请求 */
static void FinalizeOk(void)
{
    MiniFee_Context.status = MINIFEE_STATUS_OK;
    MiniFee_Context.state  = MINIFEE_STATE_IDLE;
}

/* 任务失败收尾：结果置 NOT_OK，状态机回 IDLE（上层经 GetStatus 感知失败） */
static void FinalizeErr(void)
{
    MiniFee_Context.status = MINIFEE_STATUS_NOT_OK;
    MiniFee_Context.state  = MINIFEE_STATE_IDLE;
}

/* 本次作业所需的簇内空间：一次性写用预统计总量，单块读/写用单块记录大小 */
static uint32 MiniFee_NeededSpace(void)
{
    return (MiniFee_Context.writeAllCallback != NULL_PTR)
         ? MiniFee_Context.bulkWriteNeeded
         : MINIFEE_BLOCK_TOTAL_OF(MiniFee_Context.blockSize);
}

/* 启动 FindAddr 子流程：记录完成后要返回的主状态 returnState，以及本次是读还是写 */
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

/* 异步 FindAddr 子状态机：簇选择 + 目录构建扫描 + 迁移 */
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
            uint32 gen = ClusterHdrGen(c->clusterHdrBuf);
            /* 只有 CRC 与 Magic 均有效的簇头才参与活动簇选择（无 Status），取 Generation 最大者 */
            if (gen != 0xFFFFFFFFu)
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
                c->activeClusterGen = 0xFFFFFFFFu;   /* 无有效活动簇：写簇头自增后写 1 */
                if (c->findType == FIND_TYPE_WRITE)
                {
                    c->findSubState = FIND_SUB_FRESH_ERASE;
                }
                else
                {
                    /* 空白 Flash：无任何有效簇。读请求建一个“空目录”（全 0xFFFFFFFF）后返回全零 */
                    c->activeClusterIndex  = 0u;
                    c->activeClusterStart  = MiniFee_ClusterConfig[0u].StartAddress;
                    c->activeClusterLength = MiniFee_ClusterConfig[0u].Length;
                    c->headerEnd           = MINIFEE_CLUSTER_HEADER_SIZE;
                    MiniFee_ClearDirMap();
                    c->dirValid            = 1u;
                    c->findSubState        = FIND_SUB_DONE;
                }
                break;
            }
            /* 目录已对所选活动簇有效：直接复用，跳过重建扫描（写请求空间不足时直接进迁移），
             * 省去“活动簇记录条数 + 1”次块头读；用户态无第三方改写该簇，故目录必然仍与闪存一致 */
            if ((c->dirValid != 0u) &&
                (c->bestGenIdx == c->activeClusterIndex) &&
                (c->bestGen == c->activeClusterGen))
            {
                if ((c->findType == FIND_TYPE_WRITE) &&
                    ((c->activeClusterLength - c->headerEnd) < MiniFee_NeededSpace()))
                {
                    c->findSubState = FIND_SUB_MIGRATE_PREPARE;
                }
                else
                {
                    c->findSubState = FIND_SUB_DONE;
                }
                break;
            }
            c->activeClusterIndex  = c->bestGenIdx;
            c->activeClusterStart  = MiniFee_ClusterConfig[c->bestGenIdx].StartAddress;
            c->activeClusterLength = MiniFee_ClusterConfig[c->bestGenIdx].Length;
            c->activeClusterGen    = c->bestGen;   /* 缓存活动簇 Generation，写簇头直接取用 */
            /* 无论读/写都单遍扫描活动簇块头：构建块目录并算出 headerEnd（追加写游标） */
            c->scanOff          = MINIFEE_CLUSTER_HEADER_SIZE;
            c->headerEnd        = MINIFEE_CLUSTER_HEADER_SIZE;
            c->scanCorrupt      = 0u;
            c->dirValid         = 0u;
            MiniFee_ClearDirMap();
            c->findSubState     = FIND_SUB_METRICS_READ;
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

        case FIND_SUB_WRITE_HDR_WRITE:
            /* 写簇头完成，返回调用方设定的后续子状态 */
            c->findSubState = c->writeHdrReturnState;
            break;

        case FIND_SUB_FRESH_ACTIVATE_DONE:
            c->findSubState = FIND_SUB_DONE;
            c->activeClusterIndex  = 0u;
            c->activeClusterStart  = MiniFee_ClusterConfig[0u].StartAddress;
            c->activeClusterLength = MiniFee_ClusterConfig[0u].Length;
            c->activeClusterGen    = c->writeHdrGen;   /* 新激活簇 Generation 即为刚写入值 */
            c->headerEnd       = MINIFEE_CLUSTER_HEADER_SIZE;
            MiniFee_ClearDirMap();   /* 新簇：全部块无记录 */
            c->dirValid = 1u;
            if ((c->activeClusterLength - c->headerEnd) < MiniFee_NeededSpace())
            {
                c->findResultErr = 1u;
            }
            break;

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
                /* 记录该块最新记录偏移（后扫描到的覆盖先扫描到的，最终为最新一条） */
                if (c->curHdr.BlockNumber < MINIFEE_BLOCK_COUNT)
                {
                    c->dirOffset[c->curHdr.BlockNumber] = c->scanOff;
                }
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
            if (c->scanCorrupt != 0u)
            {
                /* 损坏记录：写请求迁移回收；读请求不迁移，目录作废（下次重建） */
                if (c->findType == FIND_TYPE_WRITE)
                {
                    c->findSubState = FIND_SUB_MIGRATE_PREPARE;
                }
                else
                {
                    c->dirValid     = 0u;
                    c->findSubState = FIND_SUB_DONE;
                }
            }
            else if ((c->findType == FIND_TYPE_WRITE) &&
                     ((c->activeClusterLength - c->headerEnd) < MiniFee_NeededSpace()))
            {
                /* 剩余空间不足本次写入 -> 迁移回收 */
                c->findSubState = FIND_SUB_MIGRATE_PREPARE;
            }
            else
            {
                c->dirValid     = 1u;
                c->findSubState = FIND_SUB_DONE;
            }
            break;

        case FIND_SUB_MIGRATE_PREPARE:
            c->migrateDstIndex  = (c->activeClusterIndex + 1u) % MINIFEE_CLUSTER_COUNT;
            c->migrateDstHdrEnd = MINIFEE_CLUSTER_HEADER_SIZE;
            c->migrateDataLen   = 0u;
            c->migrateBlock     = 0u;   /* 按块目录从 0 号块起逐块迁移 */
            c->findSubState     = FIND_SUB_MIGRATE_ERASE_DST;
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
        {
            uint32 block;
            const uint8* newData;

            /* 选下一个要写入目标簇的块：优先本次作业要覆盖的块（直接写新数据，0 读），
             * 其次是源簇内有记录的块（搬运旧数据）；两者皆无则前移游标跳过。 */
            newData = NULL_PTR;
            while (c->migrateBlock < MINIFEE_BLOCK_COUNT)
            {
                newData = GetTargetData(c->migrateBlock);
                if ((newData != NULL_PTR) || (c->dirOffset[c->migrateBlock] != 0xFFFFFFFFu))
                {
                    break;
                }
                c->migrateBlock++;
            }
            if (c->migrateBlock >= MINIFEE_BLOCK_COUNT)
            {
                c->findSubState = FIND_SUB_MIGRATE_ACTIVATE;
                break;
            }

            block = c->migrateBlock;
            c->migrateBlock++;   /* 前移游标 */
            /* 块号暂存 curHdr.BlockNumber 供写完后更新目录 */
            c->migrateDataLen     = MiniFee_BlockConfig[block].Length;
            c->curHdr.BlockNumber = block;

            if (newData != NULL_PTR)
            {
                /* 本次作业要覆盖该块：直接组装新数据（迁移即写入，省去旧数据回读与迁移后补写） */
                PackNewRecord(block, c->migrateDataLen, newData);
                c->findSubState = FIND_SUB_MIGRATE_WRITE;
                break;
            }

            /* 搬运旧记录：组块头，读源记录数据到 writeBuf[8..] */
            c->curHdr.Length   = c->migrateDataLen;
            c->curHdr.CheckSum = CalcBlockChecksum(&c->curHdr);
            PackBlockHdr(&c->curHdr, c->jobBuf.writeBuf);
            if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE
                                     + c->dirOffset[block] + MINIFEE_BLOCK_HEADER_SIZE,
                                     &c->jobBuf.writeBuf[MINIFEE_BLOCK_HEADER_SIZE],
                                     MINIFEE_ALIGN_LEN(c->migrateDataLen),
                                     MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            c->findSubState = FIND_SUB_MIGRATE_WRITE;
            break;
        }

        case FIND_SUB_MIGRATE_WRITE:
        {
            uint32 writeOffset = c->migrateDstHdrEnd;
            uint32 totalLen    = MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(c->migrateDataLen));

            /* 目标簇容量防御性校验：正常配置下全部块记录必能放入单簇 */
            if ((writeOffset + totalLen) > MiniFee_ClusterConfig[c->migrateDstIndex].Length)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            if (MiniFee_StartFlsWrite(MiniFee_ClusterConfig[c->migrateDstIndex].StartAddress - MINIFEE_FLS_BASE
                                      + writeOffset,
                                      c->jobBuf.writeBuf,
                                      totalLen,
                                      MINIFEE_STATE_FIND_PROC) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            /* 目录改指目标簇内该记录位置；目标写入游标前移 */
            c->dirOffset[c->curHdr.BlockNumber] = writeOffset;
            c->migrateDstHdrEnd = writeOffset + totalLen;
            c->findSubState = FIND_SUB_MIGRATE_READ;
            break;
        }

        case FIND_SUB_MIGRATE_ACTIVATE:
            /* 激活目标簇：写目标簇头（Generation 取缓存 activeClusterGen + 1）；源簇头保持原状，不二次改写 */
            if (StartWriteClusterHdr(c->migrateDstIndex, FIND_SUB_MIGRATE_SWITCH) != E_OK)
            {
                c->findSubState = FIND_SUB_DONE; c->findResultErr = 1u; break;
            }
            break;

        case FIND_SUB_MIGRATE_SWITCH:
            /* 老簇内容保留，不再擦除源簇；仅切换活动簇（源簇待下次作为迁移目标时再擦除） */
            c->activeClusterIndex  = c->migrateDstIndex;
            c->activeClusterStart  = MiniFee_ClusterConfig[c->migrateDstIndex].StartAddress;
            c->activeClusterLength = MiniFee_ClusterConfig[c->migrateDstIndex].Length;
            c->activeClusterGen    = c->writeHdrGen;   /* 新活动簇 Generation 即为刚写入值 */
            c->headerEnd           = c->migrateDstHdrEnd;
            c->dirValid            = 1u;   /* 目录已随迁移重建为目标簇 */
            /* 本次作业要覆盖的块已在迁移阶段按新数据写入，作业的写入阶段无需再执行 */
            c->findReturnState     = MINIFEE_STATE_WRITE_DONE;
            c->findSubState        = FIND_SUB_DONE;
            break;

        case FIND_SUB_DONE:
        default:
            c->state = c->findReturnState;
            break;
    }
}

/**********************************************************************************************
* 读流程状态处理
***********************************************************************************************/
/* 单块读：按块目录直接读取目标块最新记录；无记录则返回全零。
 * 目录由 FindAddr 的目录构建扫描（冷路径）或上次操作（热路径）建立，无需再扫描记录头。 */
static void HandleReadByDir(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    if (c->findResultErr != 0u)
    {
        FinalizeErr();
        return;
    }
    if (c->dirOffset[c->blockNumber] == 0xFFFFFFFFu)
    {
        /* 目录中无该块记录：返回全零默认值 */
        CommF_DataSet(c->buf, 0u, c->logicalBlockSize);
        c->state = MINIFEE_STATE_READ_DONE;
        return;
    }
    if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE
                             + c->dirOffset[c->blockNumber] + MINIFEE_BLOCK_HEADER_SIZE,
                             c->jobBuf.blockDataBuf, c->blockSize,
                             MINIFEE_STATE_READ_VERIFY_COPY) != E_OK)
    {
        FinalizeErr();
        return;
    }
    c->scanTargetFound = 1u;
}

/* 读流程：扫描已读到 blockDataBuf。若未找到目标记录则失败；
 * 找到则只把逻辑长度数据拷给用户 buf（物理对齐填充对上层隐藏）。 */
static void HandleReadVerifyCopy(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->scanTargetFound == 0u)
    {
        /* Flash 中不存在该 Block 的记录：按默认值全零返回（而非失败） */
        CommF_DataSet(c->buf, 0u, c->logicalBlockSize);
        c->state = MINIFEE_STATE_READ_DONE;
        return;
    }
    /* 只返回逻辑数据，物理对齐填充由 MiniFee 隐藏。 */
    CommF_DataCopy(c->buf, c->jobBuf.blockDataBuf, c->logicalBlockSize);
    c->state = MINIFEE_STATE_READ_DONE;
}

/* 一次性读：按块目录逐块读取每块“最新记录”并回调交付（不再读被覆盖的旧记录）。
 * 目录由 FindAddr 构建；scanPhase=1 表示上一次数据读已完成、待交付。
 * 相比“逐条记录读数据”的实现，少读被覆盖的旧记录，进一步减少 Fls 读次数。 */
static void HandleReadAllFill(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    uint32 block;
    uint32 len;

    if (c->findResultErr != 0u)
    {
        FinalizeErr();
        return;
    }

    if (c->scanPhase == 1u)
    {
        if (c->readAllCallback != NULL_PTR)
        {
            c->readAllCallback((uint8)c->curHdr.BlockNumber, c->jobBuf.blockDataBuf, (uint32)c->curHdr.Length);
        }
        c->scanPhase = 0u;
    }

    while (c->readAllBlock < MINIFEE_BLOCK_COUNT)
    {
        block = c->readAllBlock;
        c->readAllBlock++;
        if (c->dirOffset[block] != 0xFFFFFFFFu)
        {
            len = MiniFee_BlockConfig[block].Length;
            c->curHdr.BlockNumber = (uint8)block;
            c->curHdr.Length      = (uint16)len;
            if (MiniFee_StartFlsRead(c->activeClusterStart - MINIFEE_FLS_BASE
                                     + c->dirOffset[block] + MINIFEE_BLOCK_HEADER_SIZE,
                                     c->jobBuf.blockDataBuf, MINIFEE_ALIGN_LEN(len),
                                     MINIFEE_STATE_READALL_FILL) != E_OK)
            {
                FinalizeErr();
                return;
            }
            c->scanPhase = 1u;
            return;
        }
    }
    c->state = MINIFEE_STATE_READ_DONE;
}

/**********************************************************************************************
* 写流程状态处理
***********************************************************************************************/
/* 组装“新数据”记录到 writeBuf：块头（Length=逻辑长度、CRC-8）+ 数据 + 0 填充（不含 Fls 操作）。
 * 单块写、一次性写、迁移即写入共用。 */
static void PackNewRecord(uint32 block, uint32 len, const uint8* data)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    uint32 lenAligned = MINIFEE_ALIGN_LEN(len);

    c->curHdr.BlockNumber = block;
    c->curHdr.Length      = len;
    c->curHdr.CheckSum    = CalcBlockChecksum(&c->curHdr);
    PackBlockHdr(&c->curHdr, c->jobBuf.writeBuf);
    CommF_DataCopy(&c->jobBuf.writeBuf[MINIFEE_BLOCK_HEADER_SIZE], data, len);
    CommF_DataSet(&c->jobBuf.writeBuf[MINIFEE_BLOCK_HEADER_SIZE + len], 0u, lenAligned - len);
}

/* 取本次作业要写入该块的新数据指针：NULL 表示本次作业不写该块。
 * 单块写取目标块 buf；一次性写以取数回调返回非 NULL 为准（回调须稳定，见头文件约定）。 */
static const uint8* GetTargetData(uint32 block)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->writeAllCallback != NULL_PTR)
    {
        return c->writeAllCallback((uint8)block);
    }
    return (c->blockNumber == block) ? c->buf : NULL_PTR;
}

/* 组装块头+数据并单次追加写入活动簇 headerEnd 处（不与旧记录比对：脏块过滤由上层 MiniNvm 完成）。 */
static void HandleWriteMainLoopProc(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    uint32 writeOffset;

    /* 组装块头与数据到 writeBuf，单次写入（无标志页），数据区按物理对齐长度写入 */
    PackNewRecord(c->blockNumber, c->logicalBlockSize, c->buf);

    writeOffset = c->headerEnd;
    c->headerEnd += MINIFEE_BLOCK_TOTAL_OF(c->blockSize);
    c->dirOffset[c->blockNumber] = writeOffset;   /* 更新目录：该块最新记录位置 */

    if (MiniFee_StartFlsWrite(c->activeClusterStart - MINIFEE_FLS_BASE + writeOffset,
                              c->jobBuf.writeBuf, MINIFEE_BLOCK_TOTAL_OF(c->blockSize),
                              MINIFEE_STATE_WRITE_DONE) != E_OK)
    {
        FinalizeErr();
        return;
    }
}

/* 一次性写：选簇后按块号顺序向回调取数并连续追加记录。
 * 每写一条即更新 headerEnd/块目录并前移游标，异步写完成后经 FLS_WAIT 回到本态继续。
 * 空间已在 FindAddr 阶段按 bulkWriteNeeded 预检（不足则先迁移），此处不再逐条判断。
 * writeAllBlock：当前待处理块号游标 0..MINIFEE_BLOCK_COUNT。 */
static void HandleWriteAllAppend(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    const uint8* data;
    uint32 block;
    uint32 len;
    uint32 lenAligned;
    uint32 writeOffset;

    if (c->findResultErr != 0u)
    {
        FinalizeErr();
        return;
    }

    while (c->writeAllBlock < MINIFEE_BLOCK_COUNT)
    {
        block = c->writeAllBlock;
        data = c->writeAllCallback((uint8)block);
        if (data != NULL_PTR)
        {
            len         = MiniFee_BlockConfig[block].Length;
            lenAligned  = MINIFEE_ALIGN_LEN(len);
            writeOffset = c->headerEnd;

            PackNewRecord(block, len, data);   /* 组装块头与对齐数据（真实数据 + 0 填充） */

            c->headerEnd       = writeOffset + MINIFEE_BLOCK_TOTAL_OF(lenAligned);
            c->dirOffset[block] = writeOffset;   /* 更新目录 */
            c->writeAllBlock++;   /* 先前移游标：异步完成后从下一块继续 */

            if (MiniFee_StartFlsWrite(c->activeClusterStart - MINIFEE_FLS_BASE + writeOffset,
                                      c->jobBuf.writeBuf, MINIFEE_BLOCK_TOTAL_OF(lenAligned),
                                      MINIFEE_STATE_WRITEALL_APPEND) != E_OK)
            {
                FinalizeErr();
                return;
            }
            return;   /* 等待本次异步写完成 */
        }
        c->writeAllBlock++;
    }

    /* 所有块处理完毕 */
    c->state = MINIFEE_STATE_WRITE_DONE;
}

/**********************************************************************************************
* 对外接口
***********************************************************************************************/

/* 初始化：初始化底层 Flash 适配层，并把上下文复位到 IDLE */
void MiniFee_Init(void)
{
    (void)MiniFlsIf_Init();
    MiniFee_ResetContext();
}

/* 发起异步读：参数校验通过后记录目标块与逻辑/物理长度；若块目录已就绪则直接按目录读
 * （1 次 Fls 读），否则先 FindAddr 单遍扫描构建目录。E_OK 仅表示请求被接受，结果经 GetStatus 查询。
 * 校验项：非 BUSY、buf 非空、块号合法且枚举值与下标一致、size 等于配置长度。 */
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

    MiniFee_ResetJob();
    c->blockNumber  = blockNumber;
    c->logicalBlockSize = size;
    c->blockSize    = (size + (MINIFEE_VIRTUALPAGE_SIZE - 1u)) &
                      ~(MINIFEE_VIRTUALPAGE_SIZE - 1u);
    c->buf          = buf;
    c->status       = MINIFEE_STATUS_BUSY;
    if (c->dirValid != 0u)
    {
        /* 块目录已就绪：跳过选簇/扫描，直接按目录读（0~1 次 Fls 读） */
        c->state = MINIFEE_STATE_READ_BY_DIR;
    }
    else
    {
        StartFindAddr(MINIFEE_STATE_READ_BY_DIR, FIND_TYPE_READ);   /* 冷路径：构建块目录 */
    }
    return E_OK;
}

/* 发起异步“一次性读全部块”：构建/复用块目录后，逐块读取每块最新记录并回调交付。
 * 校验：非 BUSY、回调非空。目录就绪时直接读（每块 1 次 Fls 读）；否则先单遍扫描构建目录。
 * 空白 Flash 时目录为空、回调不被触发（由上层保持默认值）。E_OK 仅表示请求被接受。 */
uint8 MiniFee_ReadAll(MiniFee_ReadAllCallbackType callback)
{
    MiniFee_ContextType* c = &MiniFee_Context;

    if (c->status == MINIFEE_STATUS_BUSY)
    {
        return E_NOT_OK;
    }
    if (callback == NULL_PTR)
    {
        return E_NOT_OK;
    }

    MiniFee_ResetJob();
    c->readAllCallback = callback;
    c->status          = MINIFEE_STATUS_BUSY;
    if (c->dirValid != 0u)
    {
        c->state = MINIFEE_STATE_READALL_FILL;   /* 目录已就绪：直接逐块读 */
    }
    else
    {
        StartFindAddr(MINIFEE_STATE_READALL_FILL, FIND_TYPE_READ);   /* 冷路径：构建块目录 */
    }
    return E_OK;
}

/* 发起异步“一次性写全部块”：先经回调预统计全部待写记录的总占用，再单遍 FindAddr 选簇
 * （空间不足则一次性迁移），随后按块号顺序连续追加所有待写记录，避免逐块 FindAddr/比对。
 * 校验：非 BUSY、回调非空。回调返回 NULL 的块跳过。E_OK 仅表示请求被接受，结果经 GetStatus 查询。 */
uint8 MiniFee_WriteAll(MiniFee_WriteAllDataCallbackType callback)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    uint32 blockNumber;
    uint32 needed;

    if (c->status == MINIFEE_STATUS_BUSY)
    {
        return E_NOT_OK;
    }
    if (callback == NULL_PTR)
    {
        return E_NOT_OK;
    }

    /* 预统计：所有待写块记录（块头 + 对齐数据）的总占用，供 FindAddr 一次性判断是否需迁移 */
    needed = 0u;
    for (blockNumber = 0u; blockNumber < MINIFEE_BLOCK_COUNT; blockNumber++)
    {
        if (callback((uint8)blockNumber) != NULL_PTR)
        {
            needed += MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(MiniFee_BlockConfig[blockNumber].Length));
        }
    }
    if (needed == 0u)
    {
        return E_OK;   /* 无待写块 */
    }

    MiniFee_ResetJob();
    c->writeAllCallback = callback;
    c->bulkWriteNeeded  = needed;
    c->writeAllBlock    = 0u;
    c->status           = MINIFEE_STATUS_BUSY;
    /* 目录已就绪、活动簇有效（非空白空目录）且空间足够：跳过选簇/迁移扫描，直接连续追加 */
    if ((c->dirValid != 0u) &&
        (c->activeClusterGen != 0xFFFFFFFFu) &&
        ((c->activeClusterLength - c->headerEnd) >= needed))
    {
        c->state = MINIFEE_STATE_WRITEALL_APPEND;
    }
    else
    {
        StartFindAddr(MINIFEE_STATE_WRITEALL_APPEND, FIND_TYPE_WRITE);   /* 选簇/迁移/构建目录 */
    }
    return E_OK;
}

/* 发起异步写：校验规则同 Read。目录已就绪且空间足够时直接进入比对/追加（旧数据读 1 次）；
 * 否则先 FindAddr 选簇/迁移/构建目录。E_OK 仅表示请求被接受，结果经 GetStatus 查询。 */
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

    MiniFee_ResetJob();
    c->blockNumber  = blockNumber;
    c->logicalBlockSize = size;
    c->blockSize    = (size + (MINIFEE_VIRTUALPAGE_SIZE - 1u)) &
                      ~(MINIFEE_VIRTUALPAGE_SIZE - 1u);
    c->buf          = buf;
    c->status       = MINIFEE_STATUS_BUSY;
    /* 目录已就绪、活动簇有效（非空白空目录）且空间足够：跳过选簇/迁移扫描，直接追加 */
    if ((c->dirValid != 0u) &&
        (c->activeClusterGen != 0xFFFFFFFFu) &&
        ((c->activeClusterLength - c->headerEnd) >= MINIFEE_BLOCK_TOTAL_OF(c->blockSize)))
    {
        c->state = MINIFEE_STATE_WRITE_MAIN_LOOP_PROC;
    }
    else
    {
        StartFindAddr(MINIFEE_STATE_WRITE_MAIN_LOOP_PROC, FIND_TYPE_WRITE);   /* 选簇/迁移/构建目录 */
    }
    return E_OK;
}

/* 取消当前异步任务：仅在 BUSY 时有效，置 NOT_OK 并回到 IDLE。
 * 注意：不撤销已在底层进行的 Flash 操作，仅让上层尽快看到失败结果。 */
uint8 MiniFee_Cancel(void)
{
    MiniFee_ContextType* c = &MiniFee_Context;
    if (c->status != MINIFEE_STATUS_BUSY)
    {
        return E_NOT_OK;
    }
    /* 取消：复位到 IDLE 并置 NOT_OK 状态；目录可能已与本簇实际状态不一致，作废以便下次重建 */
    c->status   = MINIFEE_STATUS_NOT_OK;
    c->state    = MINIFEE_STATE_IDLE;
    c->dirValid = 0u;
    return E_OK;
}

/* 状态机主入口：由调用方周期调度（在 MiniFlsIf_MainFunction 之后）。
 * 每次只根据当前 state 推进一步；IDLE 时直接返回，不阻塞等待 Flash。 */
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

        /* ----- 读 ----- */
        case MINIFEE_STATE_READ_BY_DIR:
            HandleReadByDir();
            break;
        case MINIFEE_STATE_READ_VERIFY_COPY:
            HandleReadVerifyCopy();
            break;
        case MINIFEE_STATE_READALL_FILL:
            HandleReadAllFill();
            break;
        case MINIFEE_STATE_READ_DONE:
            FinalizeOk();
            break;

        /* ----- 写 ----- */
        case MINIFEE_STATE_WRITE_MAIN_LOOP_PROC:
            HandleWriteMainLoopProc();
            break;
        case MINIFEE_STATE_WRITEALL_APPEND:
            HandleWriteAllAppend();
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

/* 查询最近一次任务状态：IDLE / BUSY / OK / NOT_OK */
MiniFee_StatusType MiniFee_GetStatus(void)
{
    return MiniFee_Context.status;
}
