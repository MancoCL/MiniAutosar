/**
 * \file Std_Types.h
 * \brief 基础类型定义（精简版，对齐 AUTOSAR Std_Types.h）
 *
 * 仅保留 MiniFee/MiniNvm 所需的最小类型集合。纯 C、无动态内存、平台无关。
 */
#ifndef STD_TYPES_H
#define STD_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ---- 布尔类型 ---- */
typedef uint8_t boolean;
#define FALSE   ((boolean)0u)
#define TRUE    ((boolean)1u)

/* ---- 整型别名（显式宽度） ---- */
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t   sint8;
typedef int16_t  sint16;
typedef int32_t  sint32;

/* ---- 标准返回类型 ---- */
typedef uint8 Std_ReturnType;
#define E_OK        ((Std_ReturnType)0u)
#define E_NOT_OK    ((Std_ReturnType)1u)

/* ---- 空指针 ---- */
#ifndef NULL_PTR
#define NULL_PTR    ((void *)0)
#endif
#ifndef NULL
#define NULL        ((void *)0)
#endif

#endif /* STD_TYPES_H */
