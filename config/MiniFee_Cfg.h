/**
 * \file MiniFee_Cfg.h
 * \brief MiniFee 配置（编译期宏）
 * \details 所有量化参数均为【假设】占位默认值，集成时必须按目标 Flash 属性对齐填写。
 *          这些值须与 FlashDrv_GetProperty() 返回的属性一致，MiniFee_Init 会校验；
 *          不一致返回 MINIFEE_ERR_PARAM。禁止运行时动态配置。
 *
 *          页布局（固定结构，字节对齐紧凑）：
 *          [页头 10B][数据区 PAGE_DATA_SIZE][页尾 6B: dataCrc(4)+status(2)]
 *          PAGE_DATA_SIZE = MINIFEE_PAGE_SIZE - 10 - 6 = MINIFEE_PAGE_SIZE - 16
 * \req P0 #4（多 cluster）、#10（简化 CRC）
 */
#ifndef MINIFEE_CFG_H
#define MINIFEE_CFG_H

#include "Std_Types.h"
#include "MiniNvm_Cfg.h"

/* ---- Cluster / 页布局（【假设】默认值，须与 Flash 驱动属性对齐） ---- */

/** cluster 数量。磨损均衡轮换单元数。约束：≥2。*/
#define MINIFEE_CLUSTER_NUM         ((uint16)0x02u)

/** 单 cluster 大小（字节）= Flash 擦除单元大小。【假设】
 *  约束（Model A）：pagesPerCluster 须 > 块数，使单个 active cluster 能容纳全部 live 块 + 1 空闲。
 *  8192 / 256 = 32 页 > 30 块，满足。 */
#define MINIFEE_CLUSTER_SIZE        ((uint32)0x2000u)

/** 单页大小（字节）。约束：≥ 块大小 + 页头尾开销(16)。
 *  默认 256 满足 30 块 × 128B（页数据区 = 256-16 = 240 ≥ 128）。【假设】 */
#define MINIFEE_PAGE_SIZE           ((uint16)256u)

/** 每 cluster 页数 = CLUSTER_SIZE / PAGE_SIZE（派生，勿手改） */
#define MINIFEE_PAGES_PER_CLUSTER   ((uint16)(MINIFEE_CLUSTER_SIZE / MINIFEE_PAGE_SIZE))

/** Flash 总容量（派生） */
#define MINIFEE_TOTAL_CAPACITY      ((uint32)(MINIFEE_CLUSTER_SIZE * MINIFEE_CLUSTER_NUM))

/** 页数据区大小（派生） */
#define MINIFEE_PAGE_DATA_SIZE      ((uint16)(MINIFEE_PAGE_SIZE - 16u))

/** NvM 块数上限（blockMap 静态数组维度）：由 MiniNvm_Cfg 的块 ID 枚举末项统一定义，勿手工填写。 */
#define MINIFEE_MAX_NUM_BLOCKS      ((uint16)MININVM_MAX_NUM_BLOCKS)

/* ---- CRC 配置（【假设】） ---- */

/** CRC 算法选择：0=CRC16，1=CRC32（默认 CRC32）。 */
#define MINIFEE_CRC_TYPE_CRC16      (0u)
#define MINIFEE_CRC_TYPE_CRC32      (1u)
#define MINIFEE_CRC_TYPE            MINIFEE_CRC_TYPE_CRC16

/** CRC32 多项式（反射型）。【假设】可改 */
#define MINIFEE_CRC32_POLY          ((uint32)0xEDB88320u)
/** CRC32 初值。【假设】 */
#define MINIFEE_CRC32_INIT          ((uint32)0xFFFFFFFFu)
/** CRC32 输出异或。【假设】 */
#define MINIFEE_CRC32_XOROUT        ((uint32)0xFFFFFFFFu)

/** CRC16 参数组（多项式/初值/输出异或）。【假设】当 MINIFEE_CRC_TYPE 选 CRC16 时使用。 */
#define MINIFEE_CRC16_POLY          ((uint16)0xA001u)
#define MINIFEE_CRC16_INIT          ((uint16)0xFFFFu)
#define MINIFEE_CRC16_XOROUT        ((uint16)0x0000u)

/* ---- 页字段魔数/状态字（Flash 1→0 友好） ---- */
/** 页头魔数 "MFEE"（4 字节，扫描时用于识别已写页）。 */
#define MINIFEE_MAGIC0              ((uint8)'M')
#define MINIFEE_MAGIC1              ((uint8)'F')
#define MINIFEE_MAGIC2              ((uint8)'E')
#define MINIFEE_MAGIC3              ((uint8)'E')

/** 页状态字：空（擦除态 0xFFFF）。 */
#define MINIFEE_STATUS_ERASED       ((uint16)0xFFFFu)
/** 页状态字：已提交有效（0x5555，1→0 友好）。 */
#define MINIFEE_STATUS_VALID        ((uint16)0x5555u)
/** 页状态字：已作废（0x0000）。 */
#define MINIFEE_STATUS_INVALID      ((uint16)0x0000u)

/* ---- GC 触发阈值 ---- */
/** 当前写 cluster 剩余空闲页 ≤ 此值时，下次写前触发 GC；0 表示仅在写满时触发。【假设】 */
#define MINIFEE_GC_THRESHOLD        ((uint16)0u)

/* ---- 编译期约束校验 ---- */
/* 页大小须容纳页头(10B)+页尾(6B)；实际块数运行时传入，MiniFee_Init 做运行时容量校验。 */
#if (MINIFEE_PAGE_SIZE < 16u)
#error "MINIFEE_PAGE_SIZE too small for header/footer"
#endif

#endif /* MINIFEE_CFG_H */
