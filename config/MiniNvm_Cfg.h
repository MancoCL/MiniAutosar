/**
 * \file MiniNvm_Cfg.h
 * \brief MiniNvm 配置（结构定义 + 常量）
 *
 * 块 ID、块配置表和 RAM mirror 均由集成方在本头文件中配置。
 * 枚举末项自动表示块数量，新增块时无需同步维护数量宏。
 */
#ifndef MININVM_CFG_H
#define MININVM_CFG_H

#include "Std_Types.h"

/* ---- 块 ID 与数量（末项必须保持为最后一项） ---- */
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
 * \brief 块配置条目。由集成方在启动时构造数组并传给 MiniNvm_Init。
 *        ramOffset 填 0，由 MiniNvm_Init 按 size 累加计算。
 */
typedef struct
{
    uint16   id;        /* 块 ID（= 索引） */
    uint16   size;      /* 块大小（逐块指定，须 ≤ MINIFEE_PAGE_DATA_SIZE） */
    uint16   ramOffset; /* RAM 镜像偏移（填 0，Init 时计算） */
    boolean  readAll;   /* 是否参与 ReadAll */
    boolean  writeAll;  /* 是否参与 WriteAll */
} MiniNvm_BlockConfigType;

/* ---- 集成方配置：新增块时同步增加枚举项和此处条目 ---- */
#define MININVM_RAM_MIRROR_SIZE   ((uint16)768u)

static const MiniNvm_BlockConfigType MiniNvm_BlockConfig[MININVM_MAX_NUM_BLOCKS] = {
    {MININVM_BLOCK_ID_0, 0x80u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_1, 0x40u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_2, 0xF0u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_3, 0x20u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_4, 0x80u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_5, 0x10u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_6, 0x60u, 0u, TRUE, TRUE},
    {MININVM_BLOCK_ID_7, 0x40u, 0u, TRUE, TRUE}
};

static uint8 MiniNvm_RamMirror[MININVM_RAM_MIRROR_SIZE];

#endif /* MININVM_CFG_H */
