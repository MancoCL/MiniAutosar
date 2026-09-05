/**
 * \file Std_Types.h
 * \brief 基础类型定义（精简版，对齐 AUTOSAR Std_Types.h）
 * \details 仅保留 MiniFee/MiniNvm 所需的最小类型集合：布尔类型、显式宽度整型别名、
 *          标准返回类型与空指针宏。纯 C99、无动态内存、平台无关。
 */
#ifndef STD_TYPES_H
#define STD_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ---- 布尔类型 ---- */
/** \brief 布尔类型（AUTOSAR boolean 语义，uint8_t 存储）：FALSE=0，TRUE=1。 */
typedef uint8_t boolean;
#define FALSE   ((boolean)0u)
#define TRUE    ((boolean)1u)

/* ---- 整型别名（显式宽度） ---- */
/** \brief 显式宽度整型别名（AUTOSAR 命名：uint8/uint16/uint32/sint8/sint16/sint32）。 */
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t   sint8;
typedef int16_t  sint16;
typedef int32_t  sint32;

/* ---- 标准返回类型 ---- */
/** \brief 标准返回类型（AUTOSAR Std_ReturnType）：E_OK=0 成功；E_NOT_OK=1 失败。 */
typedef uint8 Std_ReturnType;
#define E_OK        ((Std_ReturnType)0u)
#define E_NOT_OK    ((Std_ReturnType)1u)

/* ---- 空指针（对齐 AUTOSAR NULL_PTR；NULL 为标准库兼容） ---- */
#ifndef NULL_PTR
#define NULL_PTR    ((void *)0)
#endif
#ifndef NULL
#define NULL        ((void *)0)
#endif

#endif /* STD_TYPES_H */
