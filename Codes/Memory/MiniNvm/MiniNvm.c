/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : MiniNvm.c
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @vendor             :
 **  @version            : V1.1
 **  @description        : MiniNvm 实现：以队列缓存单块读写请求并逐个启动 MiniFee 异步作业。
 **
 **  @details            : ReadAll/WriteAll 走一次性批量路径；周期查询结果并完成数据搬运与总体结果聚合。
 **
 **  @revision           :
 **  版本      日期          编写人        CR#      描述
 **  --------  -----------   -----------   -------  ---------------------------------------------
 **  V1.0      2026/09/10    Manco         N/A      初版发布
 **  V1.1      2026/09/16    Manco         N/A      重构：一次性读写、队列仅存元数据、新增 RAM 接口
 **
 ***********************************************************************************************************************/

/* =================================================== inclusions =================================================== */
#include "MiniNvm.h"

/* =================================================== module overview =============================================== */
/**
 * @brief           模块实现总览（建议阅读顺序）。
 * @details         数据结构：单例上下文 MiniNvm_Context
 *                     - Initialized：初始化/配置校验通过标志；为 0 时拒绝一切请求；
 *                     - Current*：当前正在执行的作业（单块出队后到完成前，或一次性读/写期间）；
 *                     - Queue：单块请求环形队列，条目只存元数据（操作、块号、目标/源地址）；
 *                     - DataBuf：全模块唯一一份最大块长共享缓冲，单块读写数据经它流转；
 *                     - BlockResult/Dirty/MultiResult：单块结果、脏标记、多块总体结果。
 * @details         作业模型：
 *                     - 单块 ReadBlock/WriteBlock 经队列逐个执行（MiniFee_Read/Write）；
 *                     - ReadAll/WriteAll 为一次性批量作业：不经队列，直接调 MiniFee_ReadAll / MiniFee_WriteAll
 *                       单遍扫描/连续追加全部块，避免逐块重复扫描，效率更高。
 * @note            MiniNvm_MainFunction 每拍只做一件事：有当前作业则查询并收尾，否则从队列取下一项启动。
 *                  MiniNvm 不调用 MiniFee_MainFunction，外部必须按
 *                  MiniFlsIf_MainFunction -> MiniFee_MainFunction -> MiniNvm_MainFunction 的顺序调度。
 */


/**
 * @brief           作业操作类型
 */
typedef enum
{
    MININVM_OPERATION_READ = 0u,   /* 读 RAM <- Flash（单块，经队列） */
    MININVM_OPERATION_WRITE,       /* 写 Flash <- RAM（单块，经队列） */
    MININVM_OPERATION_READ_ALL,    /* 一次性读全部块：不经队列，直接调 MiniFee_ReadAll 单遍扫描 */
    MININVM_OPERATION_WRITE_ALL    /* 一次性写全部脏块：不经队列，直接调 MiniFee_WriteAll 单遍选簇后连续追加 */
} MiniNvm_OperationType;

/**
 * @brief           队列条目：只保存单块请求元数据（读写数据统一走上下文共享 DataBuf）
 */
typedef struct
{
    MiniNvm_OperationType Operation;    /* 读或写 */
    uint8 BlockId;                      /* MiniNvm 逻辑块号（1..N） */
    uint8* TargetAddress;               /* 读操作目标地址（写操作为 NULL） */
} MiniNvm_QueueEntryType;

/**
 * @brief           MiniNvm 全局上下文（单例）
 */
typedef struct
{
    uint8 Initialized;                              /* 初始化/配置校验通过标志（0=不可用，拒绝服务） */

    /* 当前作业上下文（CurrentValid=0 表示无当前作业） */
    uint8 CurrentValid;
    MiniNvm_OperationType CurrentOperation;         /* 当前作业是读还是写 */
    uint8 CurrentBlockId;                           /* 当前作业的 MiniNvm 块号（1..N） */
    uint8* CurrentTargetAddress;                    /* 读完成后数据要拷贝到的目标地址 */

    uint8 DataBuf[MININVM_MAX_BLOCK_LENGTH];        /* 全模块唯一共享数据缓冲（最大块长） */
    MiniNvm_QueueEntryType Queue[MININVM_QUEUE_SIZE];  /* 环形请求队列（单块请求） */
    uint8 QueueHead;                                /* 队首下标（出队处） */
    uint8 QueueTail;                                /* 队尾下标（入队处） */
    uint8 QueueCount;                               /* 当前队列长度 */

    MiniNvm_RequestResultType BlockResult[MININVM_BLOCK_COUNT];  /* 每块最近一次结果 */
    uint8 Dirty[MININVM_BLOCK_COUNT];               /* 脏标记：仅数据实际变化时置位，待 WriteAll 落盘 */
    MiniNvm_RequestResultType MultiResult;          /* 多块作业总体结果 */
} MiniNvm_ContextType;

/* ============================================ internal data definitions =========================================== */
static MiniNvm_ContextType MiniNvm_Context;

/* ========================================== internal function definitions ========================================= */
/**
 * @brief           简单字节拷贝（不依赖 libc，长度由调用方保证有效）
 * @param[out]      destination: 目标缓冲。
 * @param[in]       source: 源缓冲。
 * @param[in]       length: 拷贝字节数。
 * @return          void
 */
static void MiniNvm_Copy(uint8* destination, const uint8* source, uint32 length)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        destination[index] = source[index];
    }
}

/**
 * @brief           把一段缓冲清零（长度由调用方保证有效）
 * @param[out]      dst: 待清零缓冲。
 * @param[in]       length: 清零字节数。
 * @return          void
 */
static void MiniNvm_ClearBlock(uint8* dst, uint32 length)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        dst[index] = 0u;
    }
}

/**
 * @brief           判断待写入内容（前 length 字节 + 其后补零至块长）是否与目标块 RAM buffer 完全一致，
 * 用于写入前比对：一致则视为无变化，不复制也不置脏。
 * @param[in]       descriptor: 目标块描述符（取其 RAM buffer 与长度）。
 * @param[in]       srcPtr: 待比较数据。
 * @param[in]       length: 待比较的有效字节数，其后按 0 补足至块长再比较。
 * @return          uint8
 * @retval          1: 完全一致。
 * @retval          0: 存在差异。
 */
static uint8 MiniNvm_RamEquals(const MiniNvm_BlockDescriptorType* descriptor,
                               const uint8* srcPtr, uint32 length)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        if (descriptor->RamBlockAddress[index] != srcPtr[index])
        {
            return 0u;
        }
    }
    for (index = length; index < descriptor->Length; index++)
    {
        if (descriptor->RamBlockAddress[index] != 0u)
        {
            return 0u;
        }
    }

    return 1u;
}

/**
 * @brief           由 MiniNvm 块号（1..N）取描述符；越界返回 NULL
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @return          const MiniNvm_BlockDescriptorType*: 描述符指针。
 * @retval          NULL_PTR: 块号越界。
 */
static const MiniNvm_BlockDescriptorType* MiniNvm_GetDescriptor(uint8 blockId)
{
    if ((blockId == 0u) || (blockId > MININVM_BLOCK_COUNT))
    {
        return NULL_PTR;
    }

    return &MiniNvm_BlockDescriptor[(uint32)blockId - 1u];
}

/**
 * @brief           由 MiniFee 块号（0..N-1）取描述符；越界返回 NULL
 * @param[in]       blockNumber: MiniFee 块号（0..N-1）。
 * @return          const MiniNvm_BlockDescriptorType*: 描述符指针。
 * @retval          NULL_PTR: 块号越界。
 */
static const MiniNvm_BlockDescriptorType* MiniNvm_GetDescriptorByFeeId(MiniFee_BlockIdType blockNumber)
{
    if ((uint32)blockNumber >= MININVM_BLOCK_COUNT)
    {
        return NULL_PTR;
    }

    return &MiniNvm_BlockDescriptor[(uint32)blockNumber];
}

/**
 * @brief           MiniFee 一次性读回调：把每条记录数据拷入对应块 RAM buffer。
 * 同一块追加多条记录时后读到的（更新的）覆盖先读到的；未知块号忽略。
 * @param[in]       blockNumber: MiniFee 块号（0 起）。
 * @param[in]       data: 记录数据。
 * @param[in]       length: 逻辑长度。
 * @return          void
 */
static void MiniNvm_ReadAllCallback(uint8 blockNumber, const uint8* data, uint32 length)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptorByFeeId((MiniFee_BlockIdType)blockNumber);
    if ((descriptor != NULL_PTR) && (length == descriptor->Length))
    {
        MiniNvm_Copy(descriptor->RamBlockAddress, data, length);
    }
}

/**
 * @brief           MiniFee 一次性写取数回调：返回该块 RAM buffer 指针（待落盘）；非脏块返回 NULL 跳过。
 * @param[in]       blockNumber: MiniFee 块号（0 起）。
 * @return          const uint8*: 该块 RAM buffer 指针。
 * @retval          NULL_PTR: 非脏块（本次不写）。
 */
static const uint8* MiniNvm_WriteAllCallback(uint8 blockNumber)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptorByFeeId((MiniFee_BlockIdType)blockNumber);
    if ((descriptor == NULL_PTR) || (MiniNvm_Context.Dirty[(uint32)blockNumber] == 0u))
    {
        return NULL_PTR;
    }
    return descriptor->RamBlockAddress;
}

/**
 * @brief           判断指定块是否已有挂起请求（当前作业或队列中），用于拒绝同块重复请求
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @return          uint8
 * @retval          1: 当前作业或队列中已有该块请求。
 * @retval          0: 无挂起请求。
 */
static uint8 MiniNvm_IsBlockPending(uint8 blockId)
{
    uint8 index;
    uint8 queueIndex;

    /* 一次性读/写进行中：全部块视为挂起，拒绝新的单块请求 */
    if ((MiniNvm_Context.CurrentValid != 0u) &&
        ((MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ_ALL) ||
         (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_WRITE_ALL)))
    {
        return 1u;
    }

    /* 先看当前作业 */
    if ((MiniNvm_Context.CurrentValid != 0u) &&
        (MiniNvm_Context.CurrentBlockId == blockId))
    {
        return 1u;
    }

    queueIndex = MiniNvm_Context.QueueHead;
    for (index = 0u; index < MiniNvm_Context.QueueCount; index++)
    {
        if (MiniNvm_Context.Queue[queueIndex].BlockId == blockId)
        {
            return 1u;
        }
        queueIndex++;
        if (queueIndex >= MININVM_QUEUE_SIZE)
        {
            queueIndex = 0u;
        }
    }

    return 0u;
}

/**
 * @brief           逐项校验块描述符与 MiniFee 块配置的一致性：
 * BlockId 须等于下标+1；MiniFeeBlockId 须等于下标；Length 须等于 MiniFee 同号块 Length；
 * Length 不超过共享缓冲上限；RAM 地址非空。任一不满足即配置无效。
 * @return          uint8
 * @retval          E_OK: 描述符与 MiniFee 块配置一致。
 * @retval          E_NOT_OK: 数量/顺序/长度/RAM 地址等非法。
 */
static uint8 MiniNvm_ValidateConfig(void)
{
    uint8 index;
    const MiniNvm_BlockDescriptorType* descriptor;

    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        descriptor = &MiniNvm_BlockDescriptor[index];
        if ((descriptor->BlockId != ((uint8)index + 1u)) ||
            ((uint32)descriptor->MiniFeeBlockId != (uint32)index) ||
            (descriptor->Length > MININVM_MAX_BLOCK_LENGTH) ||
            (descriptor->Length != MiniFee_BlockConfig[index].Length) ||
            (descriptor->RamBlockAddress == NULL_PTR))
        {
            return 0u;
        }
    }

    return 1u;
}

/**
 * @brief           写入指定块（1..N）的结果；越界忽略
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @param[in]       result: 要写入的结果值。
 * @return          void
 */
static void MiniNvm_SetBlockResult(uint8 blockId, MiniNvm_RequestResultType result)
{
    if ((blockId != 0u) && (blockId <= MININVM_BLOCK_COUNT))
    {
        MiniNvm_Context.BlockResult[(uint32)blockId - 1u] = result;
    }
}

/**
 * @brief           入队一个单块请求：队列满或块号非法返回 E_NOT_OK。
 * 写请求在入队时“立即”把源数据快照进共享 DataBuf，因此入队后调用方可安全复用源缓冲。
 * 入队成功即把该块结果置 PENDING。（ReadAll/WriteAll 不经队列，走一次性读/写路径。）
 * @param[in]       operation: 操作类型（读/写）。
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @param[in]       targetAddress: 读操作目标地址；写操作传 NULL_PTR。
 * @param[in]       sourceAddress: 写操作源数据（入队即快照）；读操作传 NULL_PTR。
 * @return          uint8
 * @retval          E_OK: 入队成功，该块结果置 PENDING。
 * @retval          E_NOT_OK: 队列满或块号非法。
 */
static uint8 MiniNvm_Enqueue(MiniNvm_OperationType operation,
                             uint8 blockId,
                             uint8* targetAddress,
                             const uint8* sourceAddress)
{
    MiniNvm_QueueEntryType* entry;
    const MiniNvm_BlockDescriptorType* descriptor;

    if (MiniNvm_Context.QueueCount >= MININVM_QUEUE_SIZE)
    {
        return E_NOT_OK;
    }

    descriptor = MiniNvm_GetDescriptor(blockId);
    if (descriptor == NULL_PTR)
    {
        return E_NOT_OK;
    }

    entry = &MiniNvm_Context.Queue[MiniNvm_Context.QueueTail];
    entry->Operation = operation;
    entry->BlockId = blockId;
    entry->TargetAddress = targetAddress;
    if (operation == MININVM_OPERATION_WRITE)
    {
        /* 单块写：入队即快照进共享 DataBuf，入队后调用方可复用源缓冲 */
        MiniNvm_Copy(MiniNvm_Context.DataBuf, sourceAddress, descriptor->Length);
    }

    MiniNvm_Context.QueueTail++;
    if (MiniNvm_Context.QueueTail >= MININVM_QUEUE_SIZE)
    {
        MiniNvm_Context.QueueTail = 0u;
    }
    MiniNvm_Context.QueueCount++;
    MiniNvm_SetBlockResult(blockId, MININVM_REQ_PENDING);

    return E_OK;
}

/**
 * @brief           清空环形队列（复位头/尾/计数）
 * @return          void
 */
static void MiniNvm_ClearQueue(void)
{
    MiniNvm_Context.QueueHead = 0u;
    MiniNvm_Context.QueueTail = 0u;
    MiniNvm_Context.QueueCount = 0u;
}

/**
 * @brief           结束当前单块作业：写回单块结果，写成功时清脏标记，最后清空当前作业上下文。
 * @param[in]       result: 当前作业的最终结果。
 * @return          void
 */
static void MiniNvm_FinishCurrent(MiniNvm_RequestResultType result)
{
    MiniNvm_SetBlockResult(MiniNvm_Context.CurrentBlockId, result);
    /* 写成功才清脏标记；失败保留，便于后续重试 */
    if ((MiniNvm_Context.CurrentOperation == MININVM_OPERATION_WRITE) &&
        (result == MININVM_REQ_OK))
    {
        MiniNvm_Context.Dirty[(uint32)MiniNvm_Context.CurrentBlockId - 1u] = 0u;
    }

    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
}

/**
 * @brief           从队首取出一个请求并启动 MiniFee 作业：读出队首并前移头指针，记录当前作业上下文，
 * 然后按读/写调用 MiniFee_Read/Write（数据统一走共享 DataBuf）。启动失败立即收尾为 NOT_OK。
 * @return          void
 */
static void MiniNvm_StartCurrent(void)
{
    const MiniNvm_BlockDescriptorType* descriptor;
    MiniNvm_QueueEntryType* entry;
    uint8 result;

    /* 出队：取队首并把头指针前移（环形） */
    entry = &MiniNvm_Context.Queue[MiniNvm_Context.QueueHead];
    MiniNvm_Context.QueueHead++;
    if (MiniNvm_Context.QueueHead >= MININVM_QUEUE_SIZE)
    {
        MiniNvm_Context.QueueHead = 0u;
    }
    MiniNvm_Context.QueueCount--;

    descriptor = MiniNvm_GetDescriptor(entry->BlockId);
    MiniNvm_Context.CurrentValid = 1u;
    MiniNvm_Context.CurrentOperation = entry->Operation;
    MiniNvm_Context.CurrentBlockId = entry->BlockId;
    MiniNvm_Context.CurrentTargetAddress = entry->TargetAddress;

    if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ)
    {
        result = MiniFee_Read(descriptor->MiniFeeBlockId,
                              descriptor->Length,
                              MiniNvm_Context.DataBuf);
    }
    else
    {
        /* 写数据已在入队时快照进共享 DataBuf */
        result = MiniFee_Write(descriptor->MiniFeeBlockId,
                               descriptor->Length,
                               MiniNvm_Context.DataBuf);
    }

    if (result != E_OK)
    {
        MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
    }
}

/**
 * @brief           根据 MiniFee 状态收尾当前作业：
 *   BUSY     -> 继续等待；
 *   OK 且读  -> 把 DataBuf 拷到目标地址后置 OK；
 *   OK 且写  -> 置 OK；
 *   失败     -> 置 NOT_OK（无记录的情况已由 MiniFee 返回全零，不做默认值恢复）。
 * @param[in]       feeStatus: MiniFee 上报的任务状态。
 * @return          void
 */
static void MiniNvm_HandleCurrentResult(MiniFee_StatusType feeStatus)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptor(MiniNvm_Context.CurrentBlockId);
    if ((feeStatus == MINIFEE_STATUS_BUSY) ||
        (descriptor == NULL_PTR))
    {
        return;
    }

    if (feeStatus == MINIFEE_STATUS_OK)
    {
        if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ)
        {
            if (MiniNvm_Context.CurrentTargetAddress == NULL_PTR)
            {
                MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
            }
            else
            {
                MiniNvm_Copy(MiniNvm_Context.CurrentTargetAddress,
                             MiniNvm_Context.DataBuf,
                             descriptor->Length);
                MiniNvm_FinishCurrent(MININVM_REQ_OK);
            }
        }
        else
        {
            MiniNvm_FinishCurrent(MININVM_REQ_OK);
        }
    }
    else
    {
        /* 读/写失败：置 NOT_OK（无记录的情况已由 MiniFee 返回全零，不再做默认值恢复） */
        MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
    }
}

/**
 * @brief           收尾一次性读（MiniFee_ReadAll）：全部块结果与多块总体结果统一置位
 * @param[in]       result: 多块作业最终结果。
 * @return          void
 */
static void MiniNvm_FinishReadAll(MiniNvm_RequestResultType result)
{
    uint8 index;

    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        MiniNvm_Context.BlockResult[index] = result;
    }
    MiniNvm_Context.MultiResult = result;
    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
}

/**
 * @brief           收尾一次性写（MiniFee_WriteAll）：成功的脏块清脏并置 OK，失败的保留脏并置 NOT_OK；
 * 多块总体结果置 result。
 * @param[in]       result: 多块作业最终结果。
 * @return          void
 */
static void MiniNvm_FinishWriteAll(MiniNvm_RequestResultType result)
{
    uint8 index;

    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        if (MiniNvm_Context.Dirty[index] != 0u)
        {
            if (result == MININVM_REQ_OK)
            {
                MiniNvm_Context.Dirty[index] = 0u;
                MiniNvm_Context.BlockResult[index] = MININVM_REQ_OK;
            }
            else
            {
                MiniNvm_Context.BlockResult[index] = MININVM_REQ_NOT_OK;
            }
        }
    }
    MiniNvm_Context.MultiResult = result;
    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
}

/**
 * @brief           初始化：初始化 MiniFee，校验块描述符配置，复位队列、结果与脏标记。
 * 仅当配置有效时模块才进入可服务状态（Initialized=1），否则拒绝所有请求。
 */
/* ========================================== external function definitions ========================================= */
/**
 * @brief           初始化 MiniFee、校验块描述符、复位队列与结果。
 * @return          void
 * @note            校验失败时模块不可用，后续所有请求返回 E_NOT_OK。
 */
void MiniNvm_Init(void)
{
    uint8 index;

    MiniFee_Init();
    MiniNvm_Context.Initialized = MiniNvm_ValidateConfig();
    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
    MiniNvm_ClearQueue();
    MiniNvm_Context.MultiResult = MININVM_REQ_IDLE;
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        MiniNvm_Context.BlockResult[index] = MININVM_REQ_IDLE;
        MiniNvm_Context.Dirty[index] = 0u;
    }
}

/**
 * @brief           反初始化：复位为未初始化状态并清空队列、结果与脏标记
 * @return          void
 */
void MiniNvm_DeInit(void)
{
    uint8 index;

    /* 复位为未初始化状态：清空当前作业与队列，复位单块/多块结果 */
    MiniNvm_Context.Initialized = 0u;
    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
    MiniNvm_ClearQueue();
    MiniNvm_Context.MultiResult = MININVM_REQ_IDLE;
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        MiniNvm_Context.BlockResult[index] = MININVM_REQ_IDLE;
        MiniNvm_Context.Dirty[index] = 0u;
    }
}

/**
 * @brief           请求异步读取指定块到 dstPtr：校验状态/指针/是否已有同块挂起请求后入队（块号合法性由入队校验）。
 * E_OK 仅表示已入队，读完成后数据才会写入 dstPtr，结果经 GetErrorStatus 查询。
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @param[out]      dstPtr: 读取目标缓冲。
 * @return          uint8
 * @retval          E_OK: 已入队。
 * @retval          E_NOT_OK: 参数非法、模块未初始化或模块忙。
 */
uint8 MiniNvm_ReadBlock(uint8 blockId, uint8* dstPtr)
{
    if ((MiniNvm_Context.Initialized == 0u) ||
        (dstPtr == NULL_PTR) ||
        (MiniNvm_IsBlockPending(blockId) != 0u))
    {
        return E_NOT_OK;
    }

    return MiniNvm_Enqueue(MININVM_OPERATION_READ, blockId, dstPtr, NULL_PTR);
}

/**
 * @brief           请求异步写入指定块：校验后与当前 RAM buffer 比对，一致则直接返回（内容去重，不置脏/不入队）；
 * 不一致则置脏并入队（入队时即快照 srcPtr 数据，之后可复用源缓冲）。
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @param[in]       srcPtr: 源数据；入队即快照，入队返回后可复用。
 * @return          uint8
 * @retval          E_OK: 已入队；若与当前 RAM buffer 一致则直接返回（内容去重，不入队）。
 * @retval          E_NOT_OK: 参数非法、模块未初始化或模块忙。
 */
uint8 MiniNvm_WriteBlock(uint8 blockId, const uint8* srcPtr)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptor(blockId);
    if ((MiniNvm_Context.Initialized == 0u) ||
        (descriptor == NULL_PTR) ||
        (srcPtr == NULL_PTR) ||
        (MiniNvm_IsBlockPending(blockId) != 0u))
    {
        return E_NOT_OK;
    }

    /* 与当前 RAM buffer 一致：无变化，不置脏、不入队、结果保持不变 */
    if (MiniNvm_RamEquals(descriptor, srcPtr, descriptor->Length) != 0u)
    {
        return E_OK;
    }

    MiniNvm_Context.Dirty[(uint32)blockId - 1u] = 1u;
    return MiniNvm_Enqueue(MININVM_OPERATION_WRITE,
                           blockId,
                           NULL_PTR,
                           srcPtr);
}

/**
 * @brief           一次性读取全部块到各自 RAM buffer：先清零（Flash 中无记录的块保持默认全零），
 * 再由 MiniFee_ReadAll 单遍扫描活动簇、逐条记录回调填充，避免逐块重复扫描。
 * 结果经 MiniNvm_GetMultiJobStatus 查询；进行中拒绝新的单块/全量请求。
 * @return          uint8
 * @retval          E_OK: 请求已被接受（不代表完成）。
 * @retval          E_NOT_OK: 模块未初始化或已有作业进行中。
 */
uint8 MiniNvm_ReadAll(void)
{
    uint8 index;

    if (MiniNvm_Context.Initialized == 0u)
    {
        return E_NOT_OK;
    }
    if ((MiniNvm_Context.CurrentValid != 0u) ||
        (MiniNvm_Context.QueueCount != 0u))
    {
        return E_NOT_OK;
    }

    /* 先清零：无记录的块保持默认全零 */
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        MiniNvm_ClearBlock(MiniNvm_BlockDescriptor[index].RamBlockAddress,
                           MiniNvm_BlockDescriptor[index].Length);
    }

    MiniNvm_Context.MultiResult = MININVM_REQ_PENDING;
    MiniNvm_Context.CurrentValid = 1u;
    MiniNvm_Context.CurrentOperation = MININVM_OPERATION_READ_ALL;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;

    if (MiniFee_ReadAll(MiniNvm_ReadAllCallback) != E_OK)
    {
        MiniNvm_Context.CurrentValid = 0u;
        MiniNvm_Context.MultiResult = MININVM_REQ_NOT_OK;
        return E_NOT_OK;
    }

    return E_OK;
}

/**
 * @brief           一次性写入全部脏块到非易失存储：先统计有无脏块（无则直接 OK），再由 MiniFee_WriteAll
 * 单遍选簇后连续追加所有脏块记录（空间不足则一次性迁移），避免逐块 FindAddr/比对。
 * 结果经 MiniNvm_GetMultiJobStatus 查询；进行中拒绝新的单块请求。
 * @return          uint8
 * @retval          E_OK: 请求已被接受（不代表完成）；无脏块时不写入。
 * @retval          E_NOT_OK: 模块未初始化或已有作业进行中。
 */
uint8 MiniNvm_WriteAll(void)
{
    uint8 index;
    uint8 hasDirty;

    if (MiniNvm_Context.Initialized == 0u)
    {
        return E_NOT_OK;
    }
    if ((MiniNvm_Context.CurrentValid != 0u) ||
        (MiniNvm_Context.QueueCount != 0u))
    {
        return E_NOT_OK;
    }

    hasDirty = 0u;
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        if (MiniNvm_Context.Dirty[index] != 0u)
        {
            hasDirty = 1u;
            break;
        }
    }
    if (hasDirty == 0u)
    {
        MiniNvm_Context.MultiResult = MININVM_REQ_OK;   /* 无脏块：无需写入 */
        return E_OK;
    }

    MiniNvm_Context.MultiResult = MININVM_REQ_PENDING;
    MiniNvm_Context.CurrentValid = 1u;
    MiniNvm_Context.CurrentOperation = MININVM_OPERATION_WRITE_ALL;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;

    if (MiniFee_WriteAll(MiniNvm_WriteAllCallback) != E_OK)
    {
        MiniNvm_Context.CurrentValid = 0u;
        MiniNvm_Context.MultiResult = MININVM_REQ_NOT_OK;
        return E_NOT_OK;
    }

    return E_OK;
}

/**
 * @brief           取消当前作业与队列中所有请求：若 MiniFee 正 BUSY 则先取消它；相关单块结果置 NOT_OK，
 * 清空队列，存在被取消的作业时 MultiResult 置 NOT_OK。
 * @return          uint8
 * @retval          E_OK: 已取消。
 * @retval          E_NOT_OK: 无作业可取消。
 */
uint8 MiniNvm_CancelJobs(void)
{
    uint8 index;
    uint8 queueIndex;
    uint8 hasJobs;

    if (MiniNvm_Context.Initialized == 0u)
    {
        return E_NOT_OK;
    }

    hasJobs = ((MiniNvm_Context.CurrentValid != 0u) ||
               (MiniNvm_Context.QueueCount != 0u)) ? 1u : 0u;

    if (MiniNvm_Context.CurrentValid != 0u)
    {
        if (MiniFee_GetStatus() == MINIFEE_STATUS_BUSY)
        {
            (void)MiniFee_Cancel();
        }
        if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ_ALL)
        {
            MiniNvm_FinishReadAll(MININVM_REQ_NOT_OK);
        }
        else if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_WRITE_ALL)
        {
            MiniNvm_FinishWriteAll(MININVM_REQ_NOT_OK);
        }
        else
        {
            MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
        }
    }

    queueIndex = MiniNvm_Context.QueueHead;
    for (index = 0u; index < MiniNvm_Context.QueueCount; index++)
    {
        MiniNvm_SetBlockResult(MiniNvm_Context.Queue[queueIndex].BlockId,
                               MININVM_REQ_NOT_OK);
        queueIndex++;
        if (queueIndex >= MININVM_QUEUE_SIZE)
        {
            queueIndex = 0u;
        }
    }
    MiniNvm_ClearQueue();
    if (hasJobs != 0u)
    {
        MiniNvm_Context.MultiResult = MININVM_REQ_NOT_OK;
    }
    return E_OK;
}

/**
 * @brief           查询指定单块（blockId 从 1 开始）最近一次请求结果
 * @param[in]       blockId: MiniNvm 块号（1..N）。
 * @param[out]      resultPtr: 结果输出。
 * @return          uint8
 * @retval          E_OK: 查询成功。
 * @retval          E_NOT_OK: 参数非法。
 */
uint8 MiniNvm_GetErrorStatus(uint8 blockId, MiniNvm_RequestResultType* resultPtr)
{
    if ((MiniNvm_Context.Initialized == 0u) ||
        (resultPtr == NULL_PTR) ||
        (blockId == 0u) ||
        (blockId > MININVM_BLOCK_COUNT))
    {
        return E_NOT_OK;
    }

    *resultPtr = MiniNvm_Context.BlockResult[(uint32)blockId - 1u];
    return E_OK;
}

/**
 * @brief           查询多块作业（ReadAll/WriteAll）总体结果
 * @return          MiniNvm_RequestResultType: 多块作业总体结果。
 */
MiniNvm_RequestResultType MiniNvm_GetMultiJobStatus(void)
{
    return MiniNvm_Context.MultiResult;
}

/**
 * @brief           周期调度入口：未初始化直接返回；有当前作业则查询并处理其结果；否则队列非空时启动下一个作业
 * @return          void
 */
void MiniNvm_MainFunction(void)
{
    MiniFee_StatusType feeStatus;

    if (MiniNvm_Context.Initialized == 0u)
    {
        return;
    }

    if (MiniNvm_Context.CurrentValid != 0u)
    {
        feeStatus = MiniFee_GetStatus();
        if (feeStatus == MINIFEE_STATUS_BUSY)
        {
            return;
        }
        if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ_ALL)
        {
            MiniNvm_FinishReadAll((feeStatus == MINIFEE_STATUS_OK) ? MININVM_REQ_OK
                                                                   : MININVM_REQ_NOT_OK);
        }
        else if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_WRITE_ALL)
        {
            MiniNvm_FinishWriteAll((feeStatus == MINIFEE_STATUS_OK) ? MININVM_REQ_OK
                                                                    : MININVM_REQ_NOT_OK);
        }
        else
        {
            MiniNvm_HandleCurrentResult(feeStatus);
        }
        return;
    }

    if (MiniNvm_Context.QueueCount != 0u)
    {
        MiniNvm_StartCurrent();
    }
}

/**
 * @brief           获取指定块（MiniFee 块号，从 0 开始）的 RAM buffer 地址；不触发 Flash 作业
 * @param[in]       blockNumber: MiniFee 块号（0..N-1）。
 * @return          uint8*: RAM buffer 地址。
 * @retval          NULL_PTR: 参数非法。
 */
uint8* MiniNvm_GetRamBuffer(MiniFee_BlockIdType blockNumber)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptorByFeeId(blockNumber);
    if (descriptor == NULL_PTR)
    {
        return NULL_PTR;
    }

    return descriptor->RamBlockAddress;
}

/**
 * @brief           从指定块的 RAM buffer 同步读取 length 字节到 dstPtr；length 不得超过该块长度，不触发 Flash 作业
 * @param[in]       blockNumber: MiniFee 块号（0..N-1）。
 * @param[out]      dstPtr: 目标缓冲。
 * @param[in]       length: 读取长度，不得超过该块长度。
 * @return          uint8
 * @retval          E_OK: 读取成功。
 * @retval          E_NOT_OK: 参数非法。
 */
uint8 MiniNvm_ReadRam(MiniFee_BlockIdType blockNumber, uint8* dstPtr, uint32 length)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptorByFeeId(blockNumber);
    if ((descriptor == NULL_PTR) ||
        (dstPtr == NULL_PTR) ||
        (length > descriptor->Length))
    {
        return E_NOT_OK;
    }

    MiniNvm_Copy(dstPtr, descriptor->RamBlockAddress, length);
    return E_OK;
}

/**
 * @brief           同步写入指定块的 RAM buffer：写入 length 字节，剩余部分清零；仅当与原数据不一致时才置脏。
 * 不触发 Flash 作业；持久化需后续调用 MiniNvm_WriteAll。
 * @param[in]       blockNumber: MiniFee 块号（0..N-1）。
 * @param[in]       srcPtr: 源数据。
 * @param[in]       length: 写入长度，不得超过该块长度。
 * @return          uint8
 * @retval          E_OK: 写入成功（含与原值一致、无需改动的情形）。
 * @retval          E_NOT_OK: 参数非法。
 */
uint8 MiniNvm_WriteRam(MiniFee_BlockIdType blockNumber, const uint8* srcPtr, uint32 length)
{
    const MiniNvm_BlockDescriptorType* descriptor;
    uint32 index;

    descriptor = MiniNvm_GetDescriptorByFeeId(blockNumber);
    if ((descriptor == NULL_PTR) ||
        (srcPtr == NULL_PTR) ||
        (length > descriptor->Length))
    {
        return E_NOT_OK;
    }

    /* 与原数据一致则不做任何改动（不复制、不置脏） */
    if (MiniNvm_RamEquals(descriptor, srcPtr, length) != 0u)
    {
        return E_OK;
    }

    MiniNvm_Copy(descriptor->RamBlockAddress, srcPtr, length);
    /* 未写满的部分清零，保证块内容确定 */
    for (index = length; index < descriptor->Length; index++)
    {
        descriptor->RamBlockAddress[index] = 0u;
    }

    MiniNvm_Context.Dirty[(uint32)blockNumber] = 1u;

    return E_OK;
}