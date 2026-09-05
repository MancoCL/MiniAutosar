/**
 * \file MiniFee_Cfg.h
 * \brief MiniFee 配置（编译期宏）
 * \details 所有量化参数均为【假设】占位默认值，集成时必须按目标 Flash 属性对齐填写。
 *          这些值须与 FlashDrv_GetProperty() 返回的属性一致，MiniFee_Init 会校验；
 *          不一致返回 MINIFEE_ERR_PARAM。禁止运行时动态配置。
 *
 *          写入粒度模型（docs/02 §1）：
 *          - MINIFEE_PAGE_SIZE = 物理 Flash 最小编程字节数（如双字编程为 8）；
 *            所有 Flash 写均为整页单次编程（地址按 PAGE_SIZE 对齐、长度为其整数倍，
 *            不足填 0xFF），按 ECC 型 Flash（每页只编程一次）的保守模型设计；
 *          - FEE block（块槽）大小 = PAGE_SIZE 的整数倍：块 b 槽占
 *            blockPages = size/PAGE_SIZE + 2 页（header 页 + 数据页 + 提交页），
 *            块 size 由 MiniFee_Init 运行时逐块校验（PAGE_SIZE 整数倍、≤ MAX_BLOCK_SIZE）。
 * \req P0 #4（多 cluster）、#10（简化 CRC）
 */
#ifndef MINIFEE_CFG_H
#define MINIFEE_CFG_H

#include "Std_Types.h"
#include "MiniNvm_Cfg.h"

/* ---- Cluster / 页布局（【假设】默认值，须与 Flash 驱动属性对齐） ---- */

/** cluster 数量。磨损均衡轮换单元数。约束：≥2；建议 ≥3（docs/06：2 cluster 下 GC 中断后可能无可用备用）。*/
#define MINIFEE_CLUSTER_NUM         ((uint16)0x02u)

/** 单 cluster 大小（字节）= Flash 擦除单元大小，须为 PAGE_SIZE 整数倍。【假设】 */
#define MINIFEE_CLUSTER_SIZE        ((uint32)0x2000u)

/** 物理写页大小（字节）= 实际 Flash 最小编程字节数（如双字编程为 8）。
 *  约束：≥8（header 页 magic4+blockId2+seq2 与提交页 status2+dataLen2+crc4 的最小需求），
 *  且为 CLUSTER_SIZE 的因子。【假设】 */
#define MINIFEE_PAGE_SIZE           ((uint16)0x08u)

/** 单块数据大小上限（字节）。约束：为 PAGE_SIZE 整数倍；决定 MiniFee 内部静态工作缓冲容量
 *  （WORK_BUF_SIZE = PAGE_SIZE + 2 + MAX_BLOCK_SIZE）。【假设】 */
#define MINIFEE_MAX_BLOCK_SIZE      ((uint16)0x0100u)

/** 每 cluster 页数 = CLUSTER_SIZE / PAGE_SIZE（派生，勿手改） */
#define MINIFEE_PAGES_PER_CLUSTER   ((uint16)(MINIFEE_CLUSTER_SIZE / MINIFEE_PAGE_SIZE))

/** Flash 总容量（派生） */
#define MINIFEE_TOTAL_CAPACITY      ((uint32)(MINIFEE_CLUSTER_SIZE * MINIFEE_CLUSTER_NUM))

/** 内部静态工作缓冲大小（派生）：整页拼接 [header 页][dataLen][data] 计算 CRC 所需 */
#define MINIFEE_WORK_BUF_SIZE       ((uint16)(MINIFEE_PAGE_SIZE + 2u + MINIFEE_MAX_BLOCK_SIZE))

/** NvM 块数上限（blockMap/blockPages 静态数组维度）：由 MiniNvm_Cfg 的块 ID 枚举末项统一定义，勿手工填写。 */
#define MINIFEE_MAX_NUM_BLOCKS      ((uint16)MININVM_MAX_NUM_BLOCKS)

/* ---- CRC 配置（【假设】） ---- */

/** CRC 算法选择：0=CRC16（当前默认），1=CRC32。 */
#define MINIFEE_CRC_TYPE_CRC16      (0u)
#define MINIFEE_CRC_TYPE_CRC32      (1u)
#define MINIFEE_CRC_TYPE            MINIFEE_CRC_TYPE_CRC16

/** CRC16 参数组（反射型）。【假设】当前选用 */
#define MINIFEE_CRC16_POLY          ((uint16)0xA001u)
#define MINIFEE_CRC16_INIT          ((uint16)0xFFFFu)
#define MINIFEE_CRC16_XOROUT        ((uint16)0x0000u)

/** CRC32 参数组（反射型）。【假设】选 CRC32 时使用 */
#define MINIFEE_CRC32_POLY          ((uint32)0xEDB88320u)
#define MINIFEE_CRC32_INIT          ((uint32)0xFFFFFFFFu)
#define MINIFEE_CRC32_XOROUT        ((uint32)0xFFFFFFFFu)

/* ---- 槽字段魔数/状态字（Flash 1→0 友好，docs/02 §2/§3） ---- */
/** 槽 header 页魔数 "MFEE"（4 字节，扫描时用于识别槽首页）。 */
#define MINIFEE_MAGIC0              ((uint8)'M')
#define MINIFEE_MAGIC1              ((uint8)'F')
#define MINIFEE_MAGIC2              ((uint8)'E')
#define MINIFEE_MAGIC3              ((uint8)'E')

/** 提交页状态字：空（擦除态 0xFFFF，未提交）。 */
#define MINIFEE_STATUS_ERASED       ((uint16)0xFFFFu)
/** 提交页状态字：已提交有效（0x5555，1→0 友好）。ECC 页单次编程模型下无 INVALID 二次写作废。 */
#define MINIFEE_STATUS_VALID        ((uint16)0x5555u)

/* ---- GC 触发阈值 ---- */
/** 预留：当前仅在本次写入槽放不进 active cluster 时触发 GC；0 表示不做提前量。【假设】 */
#define MINIFEE_GC_THRESHOLD        ((uint16)0u)

/* ---- 编译期约束校验（docs/02 §2、docs/04 §1） ---- */
/* 物理写页须容纳 header 页与提交页最小元数据（各 8 字节） */
#if (MINIFEE_PAGE_SIZE < 8u)
#error "MINIFEE_PAGE_SIZE too small for header/commit page"
#endif
/* cluster 须按物理写页整除（槽不跨 cluster 的前提） */
#if ((MINIFEE_CLUSTER_SIZE % MINIFEE_PAGE_SIZE) != 0u)
#error "MINIFEE_CLUSTER_SIZE not multiple of MINIFEE_PAGE_SIZE"
#endif
/* 单块数据上限须为物理写页整数倍（块槽数据页无填充歧义） */
#if ((MINIFEE_MAX_BLOCK_SIZE % MINIFEE_PAGE_SIZE) != 0u)
#error "MINIFEE_MAX_BLOCK_SIZE not multiple of MINIFEE_PAGE_SIZE"
#endif

#endif /* MINIFEE_CFG_H */
