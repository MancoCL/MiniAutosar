/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm_Cfg.h
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniNvm 可变配置参数头文件：块数量/队列容量、块描述符类型与各块 RAM buffer 声明。
*
************************************************************************************************
*   限制         @: 无
*
************************************************************************************************
*   修订历史:
*
*   版本       日期          编写人            CR#         描述
*   --------   -----------   ----------------   --------    -----------------------
*   V1.0       2026/09/10    CaoLiang           N/A         初版发布
*
************************************************************************************************
* END_FILE_HDR*/

#ifndef MININVM_CFG_H_
#define MININVM_CFG_H_

#include "Common.h"
#include "MiniFee_Cfg.h"

/* 块数量与长度上限跟随 MiniFee 配置（MINIFEE_BLOCK_MAX / MINIFEE_MAX_BLOCK_DATA_SIZE） */
#define MININVM_BLOCK_COUNT             ((uint32)MINIFEE_BLOCK_MAX)
#define MININVM_MAX_BLOCK_LENGTH        (MINIFEE_MAX_BLOCK_DATA_SIZE)
#define MININVM_QUEUE_SIZE              (MININVM_BLOCK_COUNT)

typedef Std_ReturnType (*MiniNvm_InitBlockCallbackType)(uint8 blockId, uint8* ramPtr);

typedef struct
{
    uint8 BlockId;
    MiniFee_BlockIdType MiniFeeBlockId;
    uint32 Length;
    uint8* RamBlockAddress;
    const uint8* RomBlockAddress;
    MiniNvm_InitBlockCallbackType InitBlockCallback;
} MiniNvm_BlockDescriptorType;

/* 每个 Block 的独立 RAM buffer（与 MiniFee 块配置一一对应，长度等于对应块 Length） */
extern uint8 MiniNvm_RamBlock_1[2u];
extern uint8 MiniNvm_RamBlock_2[4u];
extern uint8 MiniNvm_RamBlock_3[4u];
extern uint8 MiniNvm_RamBlock_4[4u];
extern uint8 MiniNvm_RamBlock_5[4u];
extern uint8 MiniNvm_RamBlock_6[1u];
extern uint8 MiniNvm_RamBlock_7[1u];
extern uint8 MiniNvm_RamBlock_8[2u];
extern uint8 MiniNvm_RamBlock_9[6u];
extern uint8 MiniNvm_RamBlock_10[16u];
extern uint8 MiniNvm_RamBlock_11[16u];
extern uint8 MiniNvm_RamBlock_12[16u];
extern uint8 MiniNvm_RamBlock_13[16u];
extern uint8 MiniNvm_RamBlock_14[16u];
extern uint8 MiniNvm_RamBlock_15[16u];
extern uint8 MiniNvm_RamBlock_16[16u];
extern uint8 MiniNvm_RamBlock_17[16u];
extern uint8 MiniNvm_RamBlock_18[16u];
extern uint8 MiniNvm_RamBlock_19[16u];
extern uint8 MiniNvm_RamBlock_20[16u];
extern uint8 MiniNvm_RamBlock_21[16u];
extern uint8 MiniNvm_RamBlock_22[16u];
extern uint8 MiniNvm_RamBlock_23[16u];
extern uint8 MiniNvm_RamBlock_24[16u];
extern uint8 MiniNvm_RamBlock_25[16u];
extern uint8 MiniNvm_RamBlock_26[16u];
extern uint8 MiniNvm_RamBlock_27[16u];
extern uint8 MiniNvm_RamBlock_28[10u];
extern uint8 MiniNvm_RamBlock_29[5u];
extern uint8 MiniNvm_RamBlock_30[5u];
extern uint8 MiniNvm_RamBlock_31[3u];
extern uint8 MiniNvm_RamBlock_32[16u];
extern uint8 MiniNvm_RamBlock_33[17u];
extern uint8 MiniNvm_RamBlock_34[5u];
extern uint8 MiniNvm_RamBlock_35[10u];
extern uint8 MiniNvm_RamBlock_36[10u];
extern uint8 MiniNvm_RamBlock_37[11u];
extern uint8 MiniNvm_RamBlock_38[5u];
extern uint8 MiniNvm_RamBlock_39[5u];
extern uint8 MiniNvm_RamBlock_40[8u];
extern uint8 MiniNvm_RamBlock_41[3u];
extern uint8 MiniNvm_RamBlock_42[20u];
extern uint8 MiniNvm_RamBlock_43[5u];
extern uint8 MiniNvm_RamBlock_44[5u];
extern uint8 MiniNvm_RamBlock_45[32u];
extern uint8 MiniNvm_RamBlock_46[5u];
extern uint8 MiniNvm_RamBlock_47[5u];
extern uint8 MiniNvm_RamBlock_48[458u];
extern uint8 MiniNvm_RamBlock_49[1u];
extern uint8 MiniNvm_RamBlock_50[1u];
extern uint8 MiniNvm_RamBlock_51[2u];
extern uint8 MiniNvm_RamBlock_52[1u];
extern uint8 MiniNvm_RamBlock_53[1u];
extern uint8 MiniNvm_RamBlock_54[1u];
extern uint8 MiniNvm_RamBlock_55[112u];
extern uint8 MiniNvm_RamBlock_56[21u];
extern uint8 MiniNvm_RamBlock_57[16u];
extern uint8 MiniNvm_RamBlock_58[880u];

extern const MiniNvm_BlockDescriptorType MiniNvm_BlockDescriptor[MININVM_BLOCK_COUNT];

#endif /* MININVM_CFG_H_ */