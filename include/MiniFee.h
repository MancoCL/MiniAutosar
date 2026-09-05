/**
 * \file MiniFee.h
 * \brief MiniFee（Flash EEPROM 模拟）对外接口
 * \details 职责：在 Flash 上模拟 EEPROM，提供按 NvM 块为粒度的持久化。
 *          - Flash 划分为多个 cluster（擦除/磨损均衡轮换单元）；所有 Flash 写按
 *            MINIFEE_PAGE_SIZE（物理最小编程字节数）整页单次编程（ECC 保守模型）；
 *          - FEE block（块槽）大小 = PAGE_SIZE 的整数倍：块 b 槽占 size/PAGE_SIZE+2 页
 *            （header 页 + 数据页 + 提交页），逐块变长；
 *          - 磨损均衡：写入按 cluster 轮转（Model A，追加到 active cluster 写游标）；
 *          - 垃圾回收（GC）：槽放不进 active 时把全部 live 槽搬运到目标 cluster，
 *            擦除原 cluster 及无映射槽的垃圾 cluster；
 *          - 掉电恢复：启动扫描槽重建映射，半写/未提交槽判为无效（可丢"最新一槽"）；
 *          - 擦除块 = 写墓碑槽（dataLen=0）；无 INVALID 二次写作废；
 *          - 软件简化 CRC（不依赖 Crc 模块）。
 *
 *          访问链路硬约束：MiniFee 仅被 MiniNvm 调用，不直接对外暴露给 bootloader；
 *          MiniFee 自身通过 FlashDrv 抽象层操作硬件，绝不绕过。
 *
 *          全同步语义：函数返回即代表操作完成，无回调、无轮询状态机。
 * \req P0 #4（多 cluster）、#5（块↔槽映射）、#6（磨损均衡）、#7（GC）、#9（可丢最新一槽）、#10（简化 CRC）、#19（仅同步）
 */
#ifndef MINIFEE_H
#define MINIFEE_H

#include "Std_Types.h"

/* ---- 返回码 ---- */
typedef uint8 MiniFee_ReturnType;
#define MINIFEE_OK             ((MiniFee_ReturnType)0u)
#define MINIFEE_ERR_PARAM      ((MiniFee_ReturnType)1u)  /* 参数非法/块号越界/块大小非对齐 */
#define MINIFEE_ERR_FLASH      ((MiniFee_ReturnType)2u)  /* 底层 Flash 错误 */
#define MINIFEE_ERR_FULL       ((MiniFee_ReturnType)3u)  /* 无可用目标 cluster（GC 无落脚点） */
#define MINIFEE_ERR_CRC        ((MiniFee_ReturnType)4u)  /* CRC 校验失败 */
#define MINIFEE_ERR_NOT_FOUND  ((MiniFee_ReturnType)5u)  /* 块无有效数据（未写过/首启/墓碑） */

/* ---- 槽状态（用于查询/调试） ---- */
typedef uint8 MiniFee_PageStatusType;
#define MINIFEE_PAGE_ERASED    ((MiniFee_PageStatusType)0u)  /* 空页（0xFF，未编程） */
#define MINIFEE_PAGE_VALID     ((MiniFee_PageStatusType)1u)  /* 已提交槽（提交页 status=0x5555） */
#define MINIFEE_PAGE_DIRTY     ((MiniFee_PageStatusType)3u)  /* 半写/未提交槽（掉电残留） */

/**
 * \brief 初始化 MiniFee：校验 Flash 属性与配置一致性、逐块大小合法性与容量约束，
 *        扫描重建块→槽映射表。
 * \details 必须在 Read/Write/Erase 之前调用一次；可重复调用（模拟重启/掉电后再次扫描恢复）。
 *          逐块校验：size > 0、为 MINIFEE_PAGE_SIZE 整数倍、≤ MINIFEE_MAX_BLOCK_SIZE；
 *          容量校验：Σ blockPages + max(blockPages) ≤ pagesPerCluster（blockPages = size/PAGE_SIZE+2）。
 * \param[in] blockSizes 逐块数据大小数组（长度 = numBlocks；不可为 NULL_PTR）
 * \param[in] numBlocks 块数量（须 > 0 且 ≤ MINIFEE_MAX_NUM_BLOCKS）
 * \return MINIFEE_OK：成功；MINIFEE_ERR_PARAM：参数非法（含块大小非 PAGE_SIZE 整数倍）或
 *         Flash 属性与配置不一致；MINIFEE_ERR_FLASH：底层 Flash 错误。
 * \req P0 #4、#9
 */
MiniFee_ReturnType MiniFee_Init(const uint16 *blockSizes, uint16 numBlocks);

/**
 * \brief 读取块的最新有效槽数据到 dest。
 * \details 无有效槽（首启/未写过）或槽为墓碑（已擦除）返回 MINIFEE_ERR_NOT_FOUND；
 *          槽数据损坏返回 MINIFEE_ERR_CRC。
 * \param[in] blockId 块 ID [0, MiniFee_GetNumBlocks())
 * \param[out] dest 目标缓冲（容量 >= 该块槽数据区大小）
 * \param[out] dataLen 输出实际有效数据长度（可为 NULL_PTR，不关心时）
 * \return MINIFEE_OK：成功；MINIFEE_ERR_NOT_FOUND：无有效数据；MINIFEE_ERR_CRC：数据损坏；
 *         MINIFEE_ERR_FLASH：底层读失败；MINIFEE_ERR_PARAM：参数非法。
 * \req P0 #5、#10
 */
MiniFee_ReturnType MiniFee_ReadBlock(uint16 blockId, uint8 *dest, uint16 *dataLen);

/**
 * \brief 写入块（同步）：在 active cluster 写游标处追加新槽（整页写：
 *        header 页 → 数据页 → 提交页），提交页最后写。
 * \details 放不下时先触发 GC（搬运 live 槽到其他 cluster 并轮转）；不作废旧槽
 *          （旧槽靠 seq 竞争与 GC 回收）。提交页写入前掉电则该槽判为半写，
 *          恢复后丢弃（保持上一版本）。
 * \param[in] blockId 块 ID [0, MiniFee_GetNumBlocks())
 * \param[in] src 源数据（len=0 时写墓碑槽）
 * \param[in] len 数据长度（≤ 该块槽数据区大小）
 * \return MINIFEE_OK：成功；MINIFEE_ERR_PARAM：参数非法；MINIFEE_ERR_FULL：无可用目标 cluster；
 *         MINIFEE_ERR_FLASH：底层写失败。
 * \req P0 #5、#6、#7、#9
 */
MiniFee_ReturnType MiniFee_WriteBlock(uint16 blockId, const uint8 *src, uint16 len);

/**
 * \brief 擦除块：写入墓碑槽（dataLen=0，seq+1），使该块变为"无有效数据"。
 * \details 幂等：块无映射槽或已是墓碑时返回 MINIFEE_OK 且不再写 Flash。
 *          墓碑提交页写入前掉电则本次擦除丢失（块回到擦除前数据，"丢最新一次操作"）。
 * \param[in] blockId 块 ID [0, MiniFee_GetNumBlocks())
 * \return MINIFEE_OK：成功；MINIFEE_ERR_PARAM：参数非法；MINIFEE_ERR_FULL：无可用目标 cluster；
 *         MINIFEE_ERR_FLASH：底层写失败。
 * \req P0 #11
 */
MiniFee_ReturnType MiniFee_EraseBlock(uint16 blockId);

/**
 * \brief 查询指定块槽数据区大小（= 该块最大可存放字节数，MiniFee_Init 时传入的 size）。
 * \param[in] blockId 块 ID [0, MiniFee_GetNumBlocks())
 * \return 该块槽数据区大小（字节）；块号越界返回 0。
 */
uint16 MiniFee_GetBlockDataSize(uint16 blockId);

/**
 * \brief 查询物理写页大小（= MINIFEE_PAGE_SIZE，实际 Flash 最小编程字节数）。
 * \return 物理写页大小（字节）。
 */
uint16 MiniFee_GetWritePageSize(void);

/**
 * \brief 查询 cluster 数量。
 * \return cluster 数量。
 */
uint16 MiniFee_GetClusterCount(void);

/**
 * \brief 查询当前块数（MiniFee_Init 时传入的实际值）。
 * \return 当前块数。
 */
uint16 MiniFee_GetNumBlocks(void);

#endif /* MINIFEE_H */
