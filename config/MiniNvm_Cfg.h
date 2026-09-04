/**
 * \file MiniNvm_Cfg.h
 * \brief MiniNvm 配置（结构定义 + 常量）
 *
 * 块数量、各块大小均由集成方在启动时通过 MiniNvm_Init 传入，不在头文件中硬编码。
 * 本头文件仅定义块配置结构体、缺省填充字节、以及块数上限（用于模块内部静态数组声明）。
 */
#ifndef MININVM_CFG_H
#define MININVM_CFG_H

#include "Std_Types.h"

/**
 * 块数量上限（用于 dirty/state/error 等静态数组声明）。【假设】
 * 须与 MiniFee_Cfg 的 MINIFEE_MAX_NUM_BLOCKS 一致。
 */
#define MININVM_MAX_NUM_BLOCKS    ((uint16)64)

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

#endif /* MININVM_CFG_H */
