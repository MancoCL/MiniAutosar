/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Fls.c
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境下的 Fls 驱动 RAM 仿真实现。
 **
 **  @details            : 以 RAM 模拟 Flash，完整保留异步语义：Read/Write/Erase 只登记作业并立即返回，
 **                        Fls_MainFunction 周期推进后完成，状态经 Fls_GetStatus / Fls_GetJobResult 查询。
 **                        仿真还近似 Flash 物理约束：
 **                        1) 擦除把目标区域置 0xFF，且要求按扇区对齐；
 **                        2) 写入只允许 1 -> 0 的位清除（近似“两次擦除间只编程一次”），
 **                           若需要 0 -> 1 则判定二次编程违规并使作业失败；
 **                        3) 越界访问使作业失败并计数。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。
 **
 ***********************************************************************************************************************/

/* =================================================== inclusions =================================================== */
#include "Fls.h"
#include "Fls_Sim.h"

#include <stdio.h>

/* ===================================================== macros ===================================================== */
/**
 * @brief           仿真 Flash 几何。默认覆盖 MiniFee 示例配置的两个簇：
 *                  Cluster0 = 偏移 0x0000 / 0x2000，Cluster1 = 偏移 0x2000 / 0x2000，共 0x4000。
 * @note            若修改 MiniFee_Cfg.c 的簇地址/长度，请同步调整本处参数（保持前两个簇落在仿真范围内）。
 */
#define FLS_SIM_BASE_ADDRESS        (0xFF200000UL)
#define FLS_SIM_SIZE_BYTES          (0x4000u)
#define FLS_SIM_SECTOR_SIZE         (0x2000u)
#define FLS_SIM_PAGE_SIZE           (4u)

/**
 * @brief           作业完成所需的 Fls_MainFunction 调用次数（模拟异步时延，最小 1）。
 */
#define FLS_SIM_LATENCY_CYCLES      (1u)

/* ================================================ type definitions ================================================ */
typedef enum
{
    FLS_SIM_OP_NONE = 0,
    FLS_SIM_OP_READ,
    FLS_SIM_OP_WRITE,
    FLS_SIM_OP_ERASE
} Fls_SimOpType;

/* ============================================ external data definitions =========================================== */
uint8 Fls_Sim_Memory[FLS_SIM_SIZE_BYTES];
const uint32 Fls_Sim_Size = FLS_SIM_SIZE_BYTES;

uint32 Fls_Sim_ProgramViolations;
uint32 Fls_Sim_OutOfRange;
uint32 Fls_Sim_RejectedRequests;

/* ============================================ internal data definitions =========================================== */
static const Fls_ConfigType g_flsConfig =
{
    FLS_SIM_BASE_ADDRESS,
    FLS_SIM_SIZE_BYTES,
    FLS_SIM_SECTOR_SIZE,
    FLS_SIM_PAGE_SIZE
};

const Fls_ConfigType* const FlsConfigSet = &g_flsConfig;

static boolean             g_flsInitialized = FALSE;
static MemIf_StatusType    g_flsStatus      = MEMIF_UNINIT;
static MemIf_JobResultType g_flsJobResult   = MEMIF_JOB_OK;

static Fls_SimOpType       g_flsOp          = FLS_SIM_OP_NONE;
static uint32              g_flsOpOffset    = 0u;
static uint32              g_flsOpLength    = 0u;
static uint8*              g_flsReadDst     = NULL_PTR;
static const uint8*        g_flsWriteSrc    = NULL_PTR;
static uint32              g_flsRemain      = 0u;

/* ========================================== internal function declarations ======================================== */
static boolean Fls_Sim_RangeOk(uint32 offset, uint32 length);
static boolean Fls_Sim_Accept(Fls_SimOpType op, uint32 offset, uint32 length);
static void    Fls_Sim_Execute(void);

/* ========================================== internal function definitions ========================================= */
/**
 * @brief           判断 [offset, offset+length) 是否落在仿真 Flash 范围内。
 * @return          TRUE: 合法；FALSE: 越界。
 */
static boolean Fls_Sim_RangeOk(uint32 offset, uint32 length)
{
    if (offset > FLS_SIM_SIZE_BYTES)
    {
        return FALSE;
    }
    if (length > (FLS_SIM_SIZE_BYTES - offset))
    {
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief           公共请求受理：校验状态、长度与范围，登记作业并置 BUSY。
 * @return          TRUE: 已受理；FALSE: 拒绝（未初始化/忙/参数非法）。
 */
static boolean Fls_Sim_Accept(Fls_SimOpType op, uint32 offset, uint32 length)
{
    if ((g_flsInitialized == FALSE) || (g_flsStatus == MEMIF_BUSY))
    {
        Fls_Sim_RejectedRequests++;
        return FALSE;
    }
    if (length == 0u)
    {
        Fls_Sim_RejectedRequests++;
        return FALSE;
    }
    if (Fls_Sim_RangeOk(offset, length) == FALSE)
    {
        Fls_Sim_OutOfRange++;
        return FALSE;
    }
    if ((op == FLS_SIM_OP_ERASE) &&
        (((offset % FLS_SIM_SECTOR_SIZE) != 0u) || ((length % FLS_SIM_SECTOR_SIZE) != 0u)))
    {
        Fls_Sim_RejectedRequests++;
        return FALSE;
    }

    g_flsOp       = op;
    g_flsOpOffset = offset;
    g_flsOpLength = length;
    g_flsRemain   = FLS_SIM_LATENCY_CYCLES;
    g_flsStatus   = MEMIF_BUSY;
    g_flsJobResult = MEMIF_JOB_PENDING;
    return TRUE;
}

/**
 * @brief           执行当前登记的作业并给出结果。
 * @return          void
 */
static void Fls_Sim_Execute(void)
{
    uint32 index;

    switch (g_flsOp)
    {
        case FLS_SIM_OP_READ:
            CommF_DataCopy(g_flsReadDst, &Fls_Sim_Memory[g_flsOpOffset], g_flsOpLength);
            g_flsJobResult = MEMIF_JOB_OK;
            break;

        case FLS_SIM_OP_ERASE:
            CommF_DataSet(&Fls_Sim_Memory[g_flsOpOffset], 0xFFu, g_flsOpLength);
            g_flsJobResult = MEMIF_JOB_OK;
            break;

        case FLS_SIM_OP_WRITE:
            /* 近似“两次擦除间只编程一次”：只允许 1 -> 0。 */
            for (index = 0u; index < g_flsOpLength; index++)
            {
                uint8 old = Fls_Sim_Memory[g_flsOpOffset + index];
                uint8 newVal = g_flsWriteSrc[index];
                if ((old & newVal) != newVal)
                {
                    Fls_Sim_ProgramViolations++;
                    g_flsJobResult = MEMIF_JOB_FAILED;
                    return;
                }
            }
            CommF_DataCopy(&Fls_Sim_Memory[g_flsOpOffset], g_flsWriteSrc, g_flsOpLength);
            g_flsJobResult = MEMIF_JOB_OK;
            break;

        case FLS_SIM_OP_NONE:
        default:
            g_flsJobResult = MEMIF_JOB_FAILED;
            break;
    }
}

/* ========================================== external function definitions ========================================= */
Std_ReturnType Fls_Init(const Fls_ConfigType* ConfigPtr)
{
    if (ConfigPtr == NULL_PTR)
    {
        return E_NOT_OK;
    }

    /* 仅首次上电复位仿真介质；重复 Init 视为复位/掉电重启，保留介质内容以验证冷启动路径。 */
    if (g_flsInitialized == FALSE)
    {
        Fls_Sim_Reset();
        g_flsInitialized = TRUE;
    }

    g_flsOp       = FLS_SIM_OP_NONE;
    g_flsRemain   = 0u;
    g_flsStatus   = MEMIF_IDLE;
    g_flsJobResult = MEMIF_JOB_OK;
    return E_OK;
}

Std_ReturnType Fls_Read(uint32 offset, uint8* data, uint32 length)
{
    if (data == NULL_PTR)
    {
        Fls_Sim_RejectedRequests++;
        return E_NOT_OK;
    }
    if (Fls_Sim_Accept(FLS_SIM_OP_READ, offset, length) == FALSE)
    {
        return E_NOT_OK;
    }
    g_flsReadDst = data;
    return E_OK;
}

Std_ReturnType Fls_Write(uint32 offset, const uint8* data, uint32 length)
{
    if (data == NULL_PTR)
    {
        Fls_Sim_RejectedRequests++;
        return E_NOT_OK;
    }
    if (Fls_Sim_Accept(FLS_SIM_OP_WRITE, offset, length) == FALSE)
    {
        return E_NOT_OK;
    }
    g_flsWriteSrc = data;
    return E_OK;
}

Std_ReturnType Fls_Erase(uint32 offset, uint32 length)
{
    if (Fls_Sim_Accept(FLS_SIM_OP_ERASE, offset, length) == FALSE)
    {
        return E_NOT_OK;
    }
    return E_OK;
}

MemIf_StatusType Fls_GetStatus(void)
{
    return g_flsStatus;
}

MemIf_JobResultType Fls_GetJobResult(void)
{
    return g_flsJobResult;
}

void Fls_MainFunction(void)
{
    if ((g_flsStatus != MEMIF_BUSY) || (g_flsOp == FLS_SIM_OP_NONE))
    {
        return;
    }

    if (g_flsRemain > 0u)
    {
        g_flsRemain--;
        if (g_flsRemain > 0u)
        {
            return;
        }
    }

    Fls_Sim_Execute();
    g_flsOp     = FLS_SIM_OP_NONE;
    g_flsStatus = MEMIF_IDLE;
}

void Fls_Sim_Reset(void)
{
    CommF_DataSet(Fls_Sim_Memory, 0xFFu, FLS_SIM_SIZE_BYTES);
    Fls_Sim_ProgramViolations = 0u;
    Fls_Sim_OutOfRange        = 0u;
    Fls_Sim_RejectedRequests  = 0u;
}

void Fls_Sim_Dump(uint32 offset, uint32 length)
{
    uint32 index;
    uint32 end;

    if (Fls_Sim_RangeOk(offset, length) == FALSE)
    {
        (void)printf("[Fls_Sim_Dump] range out of bounds: offset=0x%08lX length=%lu\n",
                     (unsigned long)offset, (unsigned long)length);
        return;
    }

    end = offset + length;
    for (index = offset; index < end; index++)
    {
        if ((index % 16u) == 0u)
        {
            (void)printf("%08lX: ", (unsigned long)index);
        }
        (void)printf("%02X ", (unsigned int)Fls_Sim_Memory[index]);
        if (((index + 1u) % 16u) == 0u)
        {
            (void)printf("\n");
        }
    }
    if ((end % 16u) != 0u)
    {
        (void)printf("\n");
    }
}

/*=======[E N D   O F   F I L E]===============================================*/
