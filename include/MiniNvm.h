/**
 * \file MiniNvm.h
 * \brief MiniNvm（NVRAM 管理）对外接口
 * \details 职责：管理 NvM 块的 RAM 镜像与回写策略，作为 bootloader 与 MiniFee 之间的唯一入口。
 *          - 块类型：仅 Native 单副本；
 *          - RAM 镜像策略：上电 ReadAll 把全部块读入 RAM 镜像；运行期 WriteBlock 只更新 RAM+dirty，
 *            离开 boot 前/下电前 WriteAll 统一回写到 Flash（经 MiniFee）；
 *          - 写保护：不支持。
 *
 *          访问链路硬约束：bootloader 只能调用 MiniNvm_xxx；不得绕过 MiniNvm 直调 MiniFee/FlashDrv。
 *          全同步语义：函数返回即代表操作完成。
 *
 *          服务集：ReadBlock / WriteBlock / EraseNvBlock / GetErrorStatus / ReadAll / WriteAll。
 * \req P0 #8（访问链路）、#11（服务集齐全）、#12（仅 Native 单副本）、#13（RAM 镜像写回）、#19（仅同步）
 */
#ifndef MININVM_H
#define MININVM_H

#include "Std_Types.h"
#include "MiniNvm_Cfg.h"

/* ---- 块状态 ---- */
typedef uint8 MiniNvm_BlockStateType;
#define MININVM_BLOCK_UNINIT      ((MiniNvm_BlockStateType)0u)  /* 未初始化 */
#define MININVM_BLOCK_VALID       ((MiniNvm_BlockStateType)1u)  /* 有效（RAM/Flash 一致或已加载） */
#define MININVM_BLOCK_INVALID     ((MiniNvm_BlockStateType)2u)  /* 无效（损坏/被擦/首启缺省） */

/* ---- 错误状态位（GetErrorStatus 返回的位掩码） ---- */
typedef uint8 MiniNvm_ErrorStatusType;
#define MININVM_ERR_NONE          ((MiniNvm_ErrorStatusType)0u)
#define MININVM_ERR_CRC           ((MiniNvm_ErrorStatusType)0x01u)  /* CRC/读取损坏 */
#define MININVM_ERR_READ          ((MiniNvm_ErrorStatusType)0x02u)  /* 读取失败 */
#define MININVM_ERR_WRITE         ((MiniNvm_ErrorStatusType)0x04u)  /* 写入失败 */
#define MININVM_ERR_ERASE         ((MiniNvm_ErrorStatusType)0x08u)  /* 擦除失败 */
#define MININVM_ERR_UNINIT        ((MiniNvm_ErrorStatusType)0x10u)  /* 块未初始化 */

/**
 * \brief 初始化 MiniNvm：构建块表偏移与 RAM 镜像初态，内部完成 MiniFee 扫描恢复。
 * \details 按各块 size 累加计算 ramOffset，镜像填缺省字节，块状态置 UNINIT；
 *          配置表与 RAM 镜像由 MiniNvm_Cfg.h 静态提供。不触发读盘，读盘由 MiniNvm_ReadAll 完成。
 * \return E_OK：成功；E_NOT_OK：MiniFee_Init 失败或镜像容量不足。
 * \req P0 #15
 */
Std_ReturnType MiniNvm_Init(void);

/**
 * \brief 从 RAM 镜像读取块数据（同步，不访问 Flash）。
 * \details 块状态 VALID 才返回 E_OK；UNINIT（未 ReadAll）或 INVALID 返回 E_NOT_OK（INVALID 仍拷贝镜像）。
 * \param[in] blockId 块 ID [0, MiniNvm_GetNumBlocks())
 * \param[out] dataBuf 目标缓冲（容量 >= 该块大小）
 * \return E_OK：块有效且已拷贝；E_NOT_OK：未初始化/块号非法/空指针/状态非 VALID。
 * \req P0 #11、#13
 */
Std_ReturnType MiniNvm_ReadBlock(uint16 blockId, uint8 *dataBuf);

/**
 * \brief 写块（同步，仅更新 RAM 镜像 + dirty，不落 Flash）。
 * \details 写后块状态置 VALID；真实落盘由 MiniNvm_WriteAll 完成。
 * \param[in] blockId 块 ID [0, MiniNvm_GetNumBlocks())
 * \param[in] dataBuf 源数据（容量 >= 该块大小）
 * \return E_OK：已写入镜像；E_NOT_OK：未初始化/块号非法/空指针。
 * \req P0 #13
 */
Std_ReturnType MiniNvm_WriteBlock(uint16 blockId, const uint8 *dataBuf);

/**
 * \brief 擦除块：将 RAM 镜像置为缺省值 + dirty + 状态 INVALID。
 * \details 真实擦除（作废 Flash 页）由 MiniNvm_WriteAll 完成。
 * \param[in] blockId 块 ID [0, MiniNvm_GetNumBlocks())
 * \return E_OK：已标记；E_NOT_OK：未初始化/块号非法。
 * \req P0 #11、#13
 */
Std_ReturnType MiniNvm_EraseNvBlock(uint16 blockId);

/**
 * \brief 上电读全部：逐块从 MiniFee 读入 RAM 镜像，CRC 校验，无效块装缺省并置 INVALID。
 * \details 必须在 Init 之后、其他服务之前调用一次；部分块失败不致命，失败信息记入各块错误状态。
 * \return E_OK：完成（含部分块失败）；E_NOT_OK：未初始化。
 * \req P0 #13
 */
Std_ReturnType MiniNvm_ReadAll(void);

/**
 * \brief 全部回写：把所有 dirty 块经 MiniFee 写回 Flash，成功后清 dirty。
 * \details 推荐在跳转 APP 前/下电前调用；失败块保留 dirty 待下次重试。
 * \return E_OK：全部成功；E_NOT_OK：至少一块失败（记入块错误状态）或未初始化。
 * \req P0 #13
 */
Std_ReturnType MiniNvm_WriteAll(void);

/**
 * \brief 查询块错误状态（位掩码）。
 * \details UNINIT 块额外或上 MININVM_ERR_UNINIT；错误标志表征失败历史，块是否可读以 ReadBlock 返回为准。
 * \param[in] blockId 块 ID [0, MiniNvm_GetNumBlocks())
 * \param[out] errorStatus 输出错误状态位掩码
 * \return E_OK：成功；E_NOT_OK：未初始化/块号非法/空指针。
 * \req P0 #11
 */
Std_ReturnType MiniNvm_GetErrorStatus(uint16 blockId, MiniNvm_ErrorStatusType *errorStatus);

/**
 * \brief 查询指定块的大小（字节）。各块大小由 MiniNvm_Cfg.h 配置表逐块指定。
 * \param[in] blockId 块 ID
 * \return 块大小（字节）；blockId 非法时返回 0。
 * \req P0 #15
 */
uint16 MiniNvm_GetBlockSize(uint16 blockId);

/**
 * \brief 查询当前块数（= 配置枚举末项 MININVM_MAX_NUM_BLOCKS）。
 * \return 当前块数。
 * \req P0 #15
 */
uint16 MiniNvm_GetNumBlocks(void);

#endif /* MININVM_H */
