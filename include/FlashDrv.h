/**
 * \file FlashDrv.h
 * \brief Flash 驱动抽象层接口（平台无关）
 *
 * 屏蔽底层 Flash 硬件差异，向上（MiniFee）提供统一的读/写/擦/属性查询接口。
 * 真实目标板由移植方实现 FlashDrv_xxx；宿主机测试由 test/FlashDrv_Stub.c 实现。
 *
 * 地址模型：Fee 管理区为一段扁平地址空间 [0, totalCapacity)。
 *   - cluster i 的基地址 = i * clusterSize
 *   - cluster 内 page j 的基地址 = clusterBase + j * pageSize
 *   - 所有地址均为该扁平空间内的字节偏移。
 *
 * Flash 写约束：只能把 1 写成 0（擦除后恢复为 0xFF）。
 *   - FlashDrv_Write 不得将某位由 0 改为 1，否则返回 FLASH_ERR_PROG。
 *   - FlashDrv_EraseCluster 将整 cluster 置为 0xFF。
 *
 * 全部同步语义：函数返回即代表操作完成。
 */
#ifndef FLASHDRV_H
#define FLASHDRV_H

#include "Std_Types.h"

/* ---- 返回码 ---- */
typedef uint8 FlashDrv_ReturnType;
#define FLASH_OK              ((FlashDrv_ReturnType)0u)
#define FLASH_ERR_PARAM       ((FlashDrv_ReturnType)1u)  /* 参数非法 */
#define FLASH_ERR_PROG        ((FlashDrv_ReturnType)2u)  /* 写入未擦除区（0→1） */
#define FLASH_ERR_ERASE       ((FlashDrv_ReturnType)3u)  /* 擦除失败 */
#define FLASH_ERR_FAIL        ((FlashDrv_ReturnType)4u)  /* 通用失败 */
#define FLASH_ERR_BOUNDARY    ((FlashDrv_ReturnType)5u)  /* 越界 */

/* ---- Flash 属性 ---- */
typedef struct
{
    uint32 pageSize;        /* 单页编程粒度（字节）【假设：与 MiniFee_Cfg 对齐】 */
    uint32 clusterSize;     /* 单 cluster 大小 = 擦除单元大小（字节） */
    uint32 clusterCount;    /* cluster 数量 */
    uint32 totalCapacity;   /* 总容量 = clusterSize * clusterCount */
    uint32 writeGranularity;/* 最小写粒度（字节），1 表示支持字节编程 */
    boolean eraseAtomicity;  /* 擦除是否原子（TRUE=整 cluster 原子，FALSE=可能部分成功） */
} FlashDrv_PropertyType;

/* ---- API ---- */

/**
 * \brief 初始化 Flash 驱动。
 * \return FLASH_OK / FLASH_ERR_FAIL
 */
FlashDrv_ReturnType FlashDrv_Init(void);

/**
 * \brief 从 Fee 区读取数据。
 * \param addr 起始字节地址
 * \param len  字节数
 * \param dest 目标缓冲（调用方提供，>= len）
 * \return FLASH_OK / FLASH_ERR_PARAM / FLASH_ERR_BOUNDARY / FLASH_ERR_FAIL
 */
FlashDrv_ReturnType FlashDrv_Read(uint32 addr, uint16 len, uint8 *dest);

/**
 * \brief 向 Flash 编程（写）。目标区必须先擦除（位为 1）。
 *        只允许 1→0；尝试 0→1 返回 FLASH_ERR_PROG。
 * \param addr 起始字节地址
 * \param len  字节数
 * \param src  源数据
 * \return FLASH_OK / FLASH_ERR_PARAM / FLASH_ERR_PROG / FLASH_ERR_BOUNDARY / FLASH_ERR_FAIL
 */
FlashDrv_ReturnType FlashDrv_Write(uint32 addr, uint16 len, const uint8 *src);

/**
 * \brief 擦除指定 cluster（置为 0xFF）。
 * \param clusterIdx cluster 索引 [0, clusterCount)
 * \return FLASH_OK / FLASH_ERR_PARAM / FLASH_ERR_ERASE / FLASH_ERR_FAIL
 */
FlashDrv_ReturnType FlashDrv_EraseCluster(uint8 clusterIdx);

/**
 * \brief 查询 Flash 属性。
 * \param prop 输出属性结构
 * \return FLASH_OK / FLASH_ERR_PARAM
 */
FlashDrv_ReturnType FlashDrv_GetProperty(FlashDrv_PropertyType *prop);

#endif /* FLASHDRV_H */
