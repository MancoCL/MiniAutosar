/**
 * \file MiniNvm_Cfg.h
 * \brief MiniNvm 配置（结构定义 + 常量）
 * \details 块 ID、块配置表和 RAM mirror 均由集成方在本头文件中配置。
 *          枚举末项自动表示块数量，新增块时无需同步维护数量宏；
 *          各块大小以 MININVM_BLOCK_x_SIZE 宏为唯一数据源，
 *          MININVM_RAM_MIRROR_SIZE 由各块 size 之和自动计算，禁止手工填总容量。
 * \req P0 #15（块数与大小逐块指定）
 */
#ifndef MININVM_CFG_H
#define MININVM_CFG_H

#include "Std_Types.h"

/* ---- 块 ID 与数量（末项必须保持为最后一项） ---- */
/** \brief 块 ID 枚举。末项 MININVM_MAX_NUM_BLOCKS 自动表示块数量，新增块时在末项前追加。\req P0 #15 */
typedef enum
{
    MININVM_BLOCK_ID_0 = 0,
    MININVM_BLOCK_ID_1,
    MININVM_BLOCK_ID_2,
    MININVM_BLOCK_ID_3,
    MININVM_BLOCK_ID_4,
    MININVM_BLOCK_ID_5,
    MININVM_BLOCK_ID_6,
    MININVM_BLOCK_ID_7,
    MININVM_MAX_NUM_BLOCKS
} MiniNvm_BlockIdType;

/** 缺省数据填充字节（块无有效页/被擦除时装入 RAM 镜像）。【假设】 */
#define MININVM_DEFAULT_BYTE       ((uint8)0xFFu)

/* ---- 块配置表条目结构 ---- */
/**
 * \brief 块配置条目。由集成方在本头文件中静态构造，MiniNvm_Init(void) 自动使用。
 *        ramOffset 填 0，由 MiniNvm_Init 按 size 累加计算。
 */
typedef struct
{
    uint16   id;        /* 块 ID（= 索引） */
    uint16   size;      /* 块大小（逐块指定，须为 MINIFEE_PAGE_SIZE 整数倍且 ≤ MINIFEE_MAX_BLOCK_SIZE） */
    uint16   ramOffset; /* RAM 镜像偏移（填 0，Init 时计算） */
    boolean  readAll;   /* 是否参与 ReadAll */
    boolean  writeAll;  /* 是否参与 WriteAll */
} MiniNvm_BlockConfigType;

/* ---- 集成方配置：新增块时同步增加枚举项、大小宏、表条目和求和项 ---- */

/** 各块大小（编译期常量，配置表与 RAM 镜像容量的唯一数据源）。 */
#define MININVM_BLOCK_0_SIZE   ((uint16)0x80u)
#define MININVM_BLOCK_1_SIZE   ((uint16)0x40u)
#define MININVM_BLOCK_2_SIZE   ((uint16)0xF0u)
#define MININVM_BLOCK_3_SIZE   ((uint16)0x20u)
#define MININVM_BLOCK_4_SIZE   ((uint16)0x80u)
#define MININVM_BLOCK_5_SIZE   ((uint16)0x10u)
#define MININVM_BLOCK_6_SIZE   ((uint16)0x60u)
#define MININVM_BLOCK_7_SIZE   ((uint16)0x40u)

/** RAM 镜像总容量 = 各块 size 之和（自动计算，勿手填；MiniNvm_Init 运行时校验兜底）。 */
#define MININVM_RAM_MIRROR_SIZE \
    ((uint16)(MININVM_BLOCK_0_SIZE + MININVM_BLOCK_1_SIZE + \
              MININVM_BLOCK_2_SIZE + MININVM_BLOCK_3_SIZE + \
              MININVM_BLOCK_4_SIZE + MININVM_BLOCK_5_SIZE + \
              MININVM_BLOCK_6_SIZE + MININVM_BLOCK_7_SIZE))

/** 静态块配置表（本头文件内定义，MiniNvm_Init(void) 自动使用）。 */
static const MiniNvm_BlockConfigType MiniNvm_BlockConfig[MININVM_MAX_NUM_BLOCKS] = {
    {MININVM_BLOCK_ID_0, MININVM_BLOCK_0_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_1, MININVM_BLOCK_1_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_2, MININVM_BLOCK_2_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_3, MININVM_BLOCK_3_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_4, MININVM_BLOCK_4_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_5, MININVM_BLOCK_5_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_6, MININVM_BLOCK_6_SIZE, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_7, MININVM_BLOCK_7_SIZE, 0u, TRUE, TRUE}
};

/** 静态 RAM 镜像（容量 = MININVM_RAM_MIRROR_SIZE，本头文件内定义）。 */
static uint8 MiniNvm_RamMirror[MININVM_RAM_MIRROR_SIZE];

#endif /* MININVM_CFG_H */
