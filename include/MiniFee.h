/**
 * \file MiniFee.h
 * \brief MiniFee（Flash EEPROM 模拟）对外接口
 *
 * 职责：在 Flash 上模拟 EEPROM，提供按 NvM 块为粒度的持久化。
 *   - Flash 划分为多个 cluster（擦除/磨损均衡轮换单元）。
 *   - 一个 NvM 块 ↔ 一个 Fee 页（一一映射）；块大小 ≤ 页大小。
 *   - 磨损均衡：写入在多个 cluster 间轮流进行。
 *   - 垃圾回收（GC）：当前写 cluster 写满后，把有效页搬运到其他 cluster，再擦除原 cluster。
 *   - 掉电恢复：启动扫描页头重建映射；半写/未提交页判为无效（可丢"最新一页"）。
 *   - 软件简化 CRC（不依赖 Crc 模块）。
 *
 * 访问链路硬约束：MiniFee 仅被 MiniNvm 调用，不直接对外暴露给 bootloader。
 * MiniFee 自身通过 FlashDrv 抽象层操作硬件，绝不绕过。
 *
 * 全同步语义：函数返回即代表操作完成，无回调、无轮询状态机。
 */
#ifndef MINIFEE_H
#define MINIFEE_H

#include "Std_Types.h"

/* ---- 返回码 ---- */
typedef uint8 MiniFee_ReturnType;
#define MINIFEE_OK             ((MiniFee_ReturnType)0u)
#define MINIFEE_ERR_PARAM      ((MiniFee_ReturnType)1u)  /* 参数非法/块号越界 */
#define MINIFEE_ERR_FLASH      ((MiniFee_ReturnType)2u)  /* 底层 Flash 错误 */
#define MINIFEE_ERR_FULL       ((MiniFee_ReturnType)3u)  /* 无可用空闲页/GC 无落脚点 */
#define MINIFEE_ERR_CRC        ((MiniFee_ReturnType)4u)  /* CRC 校验失败 */
#define MINIFEE_ERR_NOT_FOUND  ((MiniFee_ReturnType)5u)  /* 块无有效页（未写过/首启） */

/* ---- 页状态（用于查询/调试） ---- */
typedef uint8 MiniFee_PageStatusType;
#define MINIFEE_PAGE_ERASED    ((MiniFee_PageStatusType)0u)  /* 空（0xFFFF） */
#define MINIFEE_PAGE_VALID     ((MiniFee_PageStatusType)1u)  /* 有效（已提交） */
#define MINIFEE_PAGE_INVALID   ((MiniFee_PageStatusType)2u)  /* 已作废 */
#define MINIFEE_PAGE_DIRTY     ((MiniFee_PageStatusType)3u)  /* 半写/未提交（掉电残留） */

/**
 * \brief 初始化 MiniFee：校验 Flash 属性与配置一致性，扫描重建块→页映射表。
 *        必须在 Read/Write/Erase 之前调用一次。
 * \param numBlocks 实际块数（须 ≤ MINIFEE_MAX_NUM_BLOCKS，且 < pagesPerCluster）
 * \return MINIFEE_OK / MINIFEE_ERR_FLASH / MINIFEE_ERR_PARAM
 */
MiniFee_ReturnType MiniFee_Init(uint16 numBlocks);

/**
 * \brief 读取块的最新有效页数据到 dest。
 *        若块无有效页（首启/未写过）返回 MINIFEE_ERR_NOT_FOUND。
 * \param blockId 块 ID [0, MiniFee_GetNumBlocks())
 * \param dest     目标缓冲（>= 该块页数据大小）
 * \param dataLen  输出实际有效数据长度
 * \return MINIFEE_OK / MINIFEE_ERR_NOT_FOUND / MINIFEE_ERR_CRC / MINIFEE_ERR_FLASH / MINIFEE_ERR_PARAM
 */
MiniFee_ReturnType MiniFee_ReadBlock(uint16 blockId, uint8 *dest, uint16 *dataLen);

/**
 * \brief 写入块（同步）：分配新页→写页头+数据+CRC→提交（VALID）→作废旧页。
 *        内部处理磨损轮转与 GC（当前写 cluster 写满时触发）。
 * \param blockId 块 ID
 * \param src     源数据
 * \param len     数据长度（≤ 页数据区大小）
 * \return MINIFEE_OK / MINIFEE_ERR_PARAM / MINIFEE_ERR_FULL / MINIFEE_ERR_FLASH
 */
MiniFee_ReturnType MiniFee_WriteBlock(uint16 blockId, const uint8 *src, uint16 len);

/**
 * \brief 擦除块（使无效）：作废该块当前有效页，使其变为"无有效页"。
 *        幂等：块本就无有效页时返回 MINIFEE_OK。
 * \param blockId 块 ID
 * \return MINIFEE_OK / MINIFEE_ERR_PARAM / MINIFEE_ERR_FLASH
 */
MiniFee_ReturnType MiniFee_EraseBlock(uint16 blockId);

/**
 * \brief 查询页数据区大小（= 块最大可存放字节数）。
 * \return 页数据区大小
 */
uint16 MiniFee_GetPageDataSize(void);

/**
 * \brief 查询 cluster 数量。
 */
uint16 MiniFee_GetClusterCount(void);

/**
 * \brief 查询当前块数（MiniFee_Init 时传入的实际值）。
 */
uint16 MiniFee_GetNumBlocks(void);

#endif /* MINIFEE_H */
