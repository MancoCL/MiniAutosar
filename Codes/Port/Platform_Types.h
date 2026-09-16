/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Platform_Types.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境的最小平台类型定义（替代目标 MCAL 的 Platform_Types.h）。
 **
 **  @note               : 仅用于在 PC（Windows/MinGW）上编译、运行与调试 MiniFee / MiniNvm / MiniFlsIf，
 **                        不参与目标工程交付。目标平台的类型由对应 MCAL 提供，勿将本文件带入目标工程。
 **
 ***********************************************************************************************************************/

#ifndef PLATFORM_TYPES_H_
#define PLATFORM_TYPES_H_

typedef unsigned char         uint8;
typedef signed char           sint8;
typedef unsigned short        uint16;
typedef signed short          sint16;
typedef unsigned int          uint32;
typedef signed int            sint32;
typedef unsigned long long    uint64;
typedef signed long long      sint64;

typedef unsigned char         boolean;
typedef float                 float32;
typedef double                float64;

#ifndef TRUE
#define TRUE                  (1u)
#endif

#ifndef FALSE
#define FALSE                 (0u)
#endif

#endif /* PLATFORM_TYPES_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
