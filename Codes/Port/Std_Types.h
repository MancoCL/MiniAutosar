/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Std_Types.h
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境的最小 AUTOSAR 标准类型定义。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。
 **
 ***********************************************************************************************************************/

#ifndef STD_TYPES_H_
#define STD_TYPES_H_

#include "Platform_Types.h"

typedef uint8 Std_ReturnType;

#ifndef E_OK
#define E_OK                  ((Std_ReturnType)0u)
#endif

#ifndef E_NOT_OK
#define E_NOT_OK              ((Std_ReturnType)1u)
#endif

#ifndef NULL_PTR
#define NULL_PTR              ((void*)0)
#endif

#ifndef STD_HIGH
#define STD_HIGH              (1u)
#endif

#ifndef STD_LOW
#define STD_LOW               (0u)
#endif

#endif /* STD_TYPES_H_ */

/*=======[E N D   O F   F I L E]===============================================*/
