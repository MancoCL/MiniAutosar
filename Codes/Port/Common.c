/**
 * Copyright (C) 2026 Manco
 *
 * 本文件为个人项目 MiniAutosar 的组成部分，与任何公司或组织无关。
 * 可自由使用、修改和分发，请保留本版权声明。
 ************************************************************************************************************************
 **
 **  @file               : Common.c
 **  @author             : Manco
 **  @date               : 2026/09/16
 **  @version            : V1.0
 **  @description        : Windows 主机验证环境的公共基础函数实现（CommF_DataSet / CommF_DataCopy）。
 **
 **  @note               : 仅用于 PC 侧验证，不参与目标工程交付。
 **
 ***********************************************************************************************************************/

/* =================================================== inclusions =================================================== */
#include "Common.h"

/* ========================================== external function definitions ========================================= */
void CommF_DataSet(void* dst, uint8 value, uint32 length)
{
    uint8* bytes = (uint8*)dst;
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        bytes[index] = value;
    }
}

void CommF_DataCopy(void* dst, const void* src, uint32 length)
{
    uint8*       out = (uint8*)dst;
    const uint8* in  = (const uint8*)src;
    uint32 index;

    for (index = 0u; index < length; index++)
    {
        out[index] = in[index];
    }
}

/*=======[E N D   O F   F I L E]===============================================*/
