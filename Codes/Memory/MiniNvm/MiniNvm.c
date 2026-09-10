/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm.c
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniNvm 实现：以环形队列缓存单块/多块读写请求，逐个启动 MiniFee 异步作业，
*                   周期查询结果并完成数据搬运、默认值恢复与总体结果聚合。
*
************************************************************************************************
*   限制         @: 无
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
#include "MiniNvm.h"

typedef enum
{
    MININVM_OPERATION_READ = 0u,
    MININVM_OPERATION_WRITE
} MiniNvm_OperationType;

typedef enum
{
    MININVM_STATE_UNINIT = 0u,
    MININVM_STATE_IDLE,
    MININVM_STATE_WAIT_MINIFEE
} MiniNvm_StateType;

typedef struct
{
    MiniNvm_OperationType Operation;
    uint8 BlockId;
    uint8 IsMulti;
    uint8* TargetAddress;
    uint8 Data[MININVM_MAX_BLOCK_LENGTH];
} MiniNvm_QueueEntryType;

typedef struct
{
    MiniNvm_StateType State;
    uint8 ConfigValid;
    uint8 CurrentValid;
    uint8 CurrentIsMulti;
    MiniNvm_OperationType CurrentOperation;
    uint8 CurrentBlockId;
    uint8* CurrentTargetAddress;
    MiniNvm_QueueEntryType CurrentEntry;
    MiniNvm_QueueEntryType Queue[MININVM_QUEUE_SIZE];
    uint8 QueueHead;
    uint8 QueueTail;
    uint8 QueueCount;
    MiniNvm_RequestResultType BlockResult[MININVM_BLOCK_COUNT];
    MiniNvm_RequestResultType MultiResult;
} MiniNvm_ContextType;

static MiniNvm_ContextType MiniNvm_Context;

static void MiniNvm_Copy(uint8* destination, const uint8* source, uint32 length)
{
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        destination[index] = source[index];
    }
}

static const MiniNvm_BlockDescriptorType* MiniNvm_GetDescriptor(uint8 blockId)
{
    if ((blockId == 0u) || (blockId > MININVM_BLOCK_COUNT))
    {
        return NULL_PTR;
    }

    return &MiniNvm_BlockDescriptor[(uint32)blockId - 1u];
}

static uint8 MiniNvm_IsBlockPending(uint8 blockId)
{
    uint8 index;
    uint8 queueIndex;

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

static void MiniNvm_SetBlockResult(uint8 blockId, MiniNvm_RequestResultType result)
{
    if ((blockId != 0u) && (blockId <= MININVM_BLOCK_COUNT))
    {
        MiniNvm_Context.BlockResult[(uint32)blockId - 1u] = result;
    }
}

static uint8 MiniNvm_Enqueue(MiniNvm_OperationType operation,
                             uint8 blockId,
                             uint8* targetAddress,
                             const uint8* sourceAddress,
                             uint8 isMulti)
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
    entry->IsMulti = isMulti;
    entry->TargetAddress = targetAddress;
    if (operation == MININVM_OPERATION_WRITE)
    {
        MiniNvm_Copy(entry->Data, sourceAddress, descriptor->Length);
    }

    MiniNvm_Context.QueueTail++;
    if (MiniNvm_Context.QueueTail >= MININVM_QUEUE_SIZE)
    {
        MiniNvm_Context.QueueTail = 0u;
    }
    MiniNvm_Context.QueueCount++;
    MiniNvm_SetBlockResult(blockId, MININVM_REQ_PENDING);
    if (isMulti != 0u)
    {
        MiniNvm_Context.MultiResult = MININVM_REQ_PENDING;
    }

    return E_OK;
}

static void MiniNvm_ClearQueue(void)
{
    MiniNvm_Context.QueueHead = 0u;
    MiniNvm_Context.QueueTail = 0u;
    MiniNvm_Context.QueueCount = 0u;
}

static uint8 MiniNvm_RestoreCurrent(void)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptor(MiniNvm_Context.CurrentBlockId);
    if ((descriptor == NULL_PTR) || (MiniNvm_Context.CurrentTargetAddress == NULL_PTR))
    {
        return E_NOT_OK;
    }

    if (descriptor->RomBlockAddress != NULL_PTR)
    {
        MiniNvm_Copy(MiniNvm_Context.CurrentTargetAddress,
                     descriptor->RomBlockAddress,
                     descriptor->Length);
        return E_OK;
    }

    if (descriptor->InitBlockCallback != NULL_PTR)
    {
        return descriptor->InitBlockCallback(MiniNvm_Context.CurrentBlockId,
                                             MiniNvm_Context.CurrentTargetAddress);
    }

    return E_NOT_OK;
}

static void MiniNvm_FinishCurrent(MiniNvm_RequestResultType result)
{
    MiniNvm_SetBlockResult(MiniNvm_Context.CurrentBlockId, result);
    if (MiniNvm_Context.CurrentIsMulti != 0u)
    {
        if ((result == MININVM_REQ_NOT_OK) ||
            (MiniNvm_Context.MultiResult == MININVM_REQ_NOT_OK))
        {
            MiniNvm_Context.MultiResult = MININVM_REQ_NOT_OK;
        }
        else if (MiniNvm_Context.QueueCount == 0u)
        {
            MiniNvm_Context.MultiResult = MININVM_REQ_OK;
        }
    }

    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentIsMulti = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
    MiniNvm_Context.State = MININVM_STATE_IDLE;
}

static void MiniNvm_StartCurrent(void)
{
    const MiniNvm_BlockDescriptorType* descriptor;
    uint8 result;

    MiniNvm_Context.CurrentEntry = MiniNvm_Context.Queue[MiniNvm_Context.QueueHead];
    MiniNvm_Context.QueueHead++;
    if (MiniNvm_Context.QueueHead >= MININVM_QUEUE_SIZE)
    {
        MiniNvm_Context.QueueHead = 0u;
    }
    MiniNvm_Context.QueueCount--;

    descriptor = MiniNvm_GetDescriptor(MiniNvm_Context.CurrentEntry.BlockId);
    MiniNvm_Context.CurrentValid = 1u;
    MiniNvm_Context.CurrentOperation = MiniNvm_Context.CurrentEntry.Operation;
    MiniNvm_Context.CurrentBlockId = MiniNvm_Context.CurrentEntry.BlockId;
    MiniNvm_Context.CurrentTargetAddress = MiniNvm_Context.CurrentEntry.TargetAddress;
    MiniNvm_Context.CurrentIsMulti = MiniNvm_Context.CurrentEntry.IsMulti;
    MiniNvm_Context.State = MININVM_STATE_WAIT_MINIFEE;

    if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ)
    {
        result = MiniFee_Read(descriptor->MiniFeeBlockId,
                              descriptor->Length,
                              MiniNvm_Context.CurrentEntry.Data);
    }
    else
    {
        result = MiniFee_Write(descriptor->MiniFeeBlockId,
                               descriptor->Length,
                               MiniNvm_Context.CurrentEntry.Data);
    }

    if (result != E_OK)
    {
        MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
    }
}

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
                             MiniNvm_Context.CurrentEntry.Data,
                             descriptor->Length);
                MiniNvm_FinishCurrent(MININVM_REQ_OK);
            }
        }
        else
        {
            MiniNvm_FinishCurrent(MININVM_REQ_OK);
        }
    }
    else if (MiniNvm_Context.CurrentOperation == MININVM_OPERATION_READ)
    {
        if (MiniNvm_RestoreCurrent() == E_OK)
        {
            MiniNvm_FinishCurrent(MININVM_REQ_RESTORED_FROM_ROM);
        }
        else
        {
            MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
        }
    }
    else
    {
        MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
    }
}

static uint8 MiniNvm_StartAll(MiniNvm_OperationType operation)
{
    uint8 index;
    const MiniNvm_BlockDescriptorType* descriptor;

    if ((MiniNvm_Context.CurrentValid != 0u) ||
        (MiniNvm_Context.QueueCount != 0u))
    {
        return E_NOT_OK;
    }

    MiniNvm_Context.MultiResult = MININVM_REQ_PENDING;
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        descriptor = &MiniNvm_BlockDescriptor[index];
        if (MiniNvm_Enqueue(operation,
                            descriptor->BlockId,
                            descriptor->RamBlockAddress,
                            descriptor->RamBlockAddress,
                            1u) != E_OK)
        {
            MiniNvm_ClearQueue();
            MiniNvm_Context.MultiResult = MININVM_REQ_NOT_OK;
            return E_NOT_OK;
        }
    }

    return E_OK;
}

void MiniNvm_Init(void)
{
    uint8 index;

    MiniFee_Init();
    MiniNvm_Context.State = MININVM_STATE_UNINIT;
    MiniNvm_Context.ConfigValid = MiniNvm_ValidateConfig();
    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentIsMulti = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
    MiniNvm_ClearQueue();
    MiniNvm_Context.MultiResult = MININVM_REQ_IDLE;
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        MiniNvm_Context.BlockResult[index] = MININVM_REQ_IDLE;
    }

    if (MiniNvm_Context.ConfigValid != 0u)
    {
        MiniNvm_Context.State = MININVM_STATE_IDLE;
    }
}

void MiniNvm_DeInit(void)
{
    uint8 index;

    /* 复位为未初始化状态：清空当前作业与队列，复位单块/多块结果 */
    MiniNvm_Context.State = MININVM_STATE_UNINIT;
    MiniNvm_Context.ConfigValid = 0u;
    MiniNvm_Context.CurrentValid = 0u;
    MiniNvm_Context.CurrentIsMulti = 0u;
    MiniNvm_Context.CurrentTargetAddress = NULL_PTR;
    MiniNvm_ClearQueue();
    MiniNvm_Context.MultiResult = MININVM_REQ_IDLE;
    for (index = 0u; index < MININVM_BLOCK_COUNT; index++)
    {
        MiniNvm_Context.BlockResult[index] = MININVM_REQ_IDLE;
    }
}

uint8 MiniNvm_ReadBlock(uint8 blockId, uint8* dstPtr)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptor(blockId);
    if ((MiniNvm_Context.State == MININVM_STATE_UNINIT) ||
        (descriptor == NULL_PTR) ||
        (dstPtr == NULL_PTR) ||
        (MiniNvm_IsBlockPending(blockId) != 0u))
    {
        return E_NOT_OK;
    }

    return MiniNvm_Enqueue(MININVM_OPERATION_READ, blockId, dstPtr, NULL_PTR, 0u);
}

uint8 MiniNvm_WriteBlock(uint8 blockId, const uint8* srcPtr)
{
    const MiniNvm_BlockDescriptorType* descriptor;

    descriptor = MiniNvm_GetDescriptor(blockId);
    if ((MiniNvm_Context.State == MININVM_STATE_UNINIT) ||
        (descriptor == NULL_PTR) ||
        (srcPtr == NULL_PTR) ||
        (MiniNvm_IsBlockPending(blockId) != 0u))
    {
        return E_NOT_OK;
    }

    return MiniNvm_Enqueue(MININVM_OPERATION_WRITE,
                           blockId,
                           NULL_PTR,
                           srcPtr,
                           0u);
}

uint8 MiniNvm_ReadAll(void)
{
    if (MiniNvm_Context.State == MININVM_STATE_UNINIT)
    {
        return E_NOT_OK;
    }
    return MiniNvm_StartAll(MININVM_OPERATION_READ);
}

uint8 MiniNvm_WriteAll(void)
{
    if (MiniNvm_Context.State == MININVM_STATE_UNINIT)
    {
        return E_NOT_OK;
    }
    return MiniNvm_StartAll(MININVM_OPERATION_WRITE);
}

uint8 MiniNvm_CancelJobs(void)
{
    uint8 index;
    uint8 queueIndex;
    uint8 hasJobs;

    if (MiniNvm_Context.State == MININVM_STATE_UNINIT)
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
        MiniNvm_FinishCurrent(MININVM_REQ_NOT_OK);
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

uint8 MiniNvm_RestoreBlockDefaults(uint8 blockId)
{
    const MiniNvm_BlockDescriptorType* descriptor;
    uint8 result;

    descriptor = MiniNvm_GetDescriptor(blockId);
    if ((MiniNvm_Context.State == MININVM_STATE_UNINIT) ||
        (descriptor == NULL_PTR) ||
        (descriptor->RamBlockAddress == NULL_PTR) ||
        (MiniNvm_IsBlockPending(blockId) != 0u))
    {
        return E_NOT_OK;
    }

    if (descriptor->RomBlockAddress != NULL_PTR)
    {
        MiniNvm_Copy(descriptor->RamBlockAddress,
                     descriptor->RomBlockAddress,
                     descriptor->Length);
        result = E_OK;
    }
    else if (descriptor->InitBlockCallback != NULL_PTR)
    {
        result = descriptor->InitBlockCallback(blockId, descriptor->RamBlockAddress);
    }
    else
    {
        result = E_NOT_OK;
    }

    MiniNvm_SetBlockResult(blockId,
                           (result == E_OK) ? MININVM_REQ_RESTORED_FROM_ROM : MININVM_REQ_NOT_OK);
    return result;
}

uint8 MiniNvm_GetErrorStatus(uint8 blockId, MiniNvm_RequestResultType* resultPtr)
{
    if ((MiniNvm_Context.State == MININVM_STATE_UNINIT) ||
        (resultPtr == NULL_PTR) ||
        (blockId == 0u) ||
        (blockId > MININVM_BLOCK_COUNT))
    {
        return E_NOT_OK;
    }

    *resultPtr = MiniNvm_Context.BlockResult[(uint32)blockId - 1u];
    return E_OK;
}

MiniNvm_RequestResultType MiniNvm_GetMultiJobStatus(void)
{
    return MiniNvm_Context.MultiResult;
}

void MiniNvm_MainFunction(void)
{
    MiniFee_StatusType feeStatus;

    if (MiniNvm_Context.State == MININVM_STATE_UNINIT)
    {
        return;
    }

    if (MiniNvm_Context.CurrentValid != 0u)
    {
        feeStatus = MiniFee_GetStatus();
        MiniNvm_HandleCurrentResult(feeStatus);
        return;
    }

    if (MiniNvm_Context.QueueCount != 0u)
    {
        MiniNvm_StartCurrent();
    }
}
