/*  BEGIN_FILE_HDR
******************************************Copyright(C)*****************************************
*
*                                       YKXH  Technology
*
***********************************文件信息***************************************************
*   文件名       @: MiniNvm_Cfg.c
************************************************************************************************
*   工程/产品     @:
*   标题         @:
*   作者         @: CaoLiang
************************************************************************************************
*   描述         @: MiniNvm 可变配置数据定义：各块独立 RAM buffer 与块描述符表。
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
#include "MiniNvm_Cfg.h"

/* 每个 Block 的独立 RAM buffer 定义（与 MiniFee 块配置一一对应） */
uint8 MiniNvm_RamBlock_1[2u];
uint8 MiniNvm_RamBlock_2[4u];
uint8 MiniNvm_RamBlock_3[4u];
uint8 MiniNvm_RamBlock_4[4u];
uint8 MiniNvm_RamBlock_5[4u];
uint8 MiniNvm_RamBlock_6[1u];
uint8 MiniNvm_RamBlock_7[1u];
uint8 MiniNvm_RamBlock_8[2u];
uint8 MiniNvm_RamBlock_9[6u];
uint8 MiniNvm_RamBlock_10[16u];
uint8 MiniNvm_RamBlock_11[16u];
uint8 MiniNvm_RamBlock_12[16u];
uint8 MiniNvm_RamBlock_13[16u];
uint8 MiniNvm_RamBlock_14[16u];
uint8 MiniNvm_RamBlock_15[16u];
uint8 MiniNvm_RamBlock_16[16u];
uint8 MiniNvm_RamBlock_17[16u];
uint8 MiniNvm_RamBlock_18[16u];
uint8 MiniNvm_RamBlock_19[16u];
uint8 MiniNvm_RamBlock_20[16u];
uint8 MiniNvm_RamBlock_21[16u];
uint8 MiniNvm_RamBlock_22[16u];
uint8 MiniNvm_RamBlock_23[16u];
uint8 MiniNvm_RamBlock_24[16u];
uint8 MiniNvm_RamBlock_25[16u];
uint8 MiniNvm_RamBlock_26[16u];
uint8 MiniNvm_RamBlock_27[16u];
uint8 MiniNvm_RamBlock_28[10u];
uint8 MiniNvm_RamBlock_29[5u];
uint8 MiniNvm_RamBlock_30[5u];
uint8 MiniNvm_RamBlock_31[3u];
uint8 MiniNvm_RamBlock_32[16u];
uint8 MiniNvm_RamBlock_33[17u];
uint8 MiniNvm_RamBlock_34[5u];
uint8 MiniNvm_RamBlock_35[10u];
uint8 MiniNvm_RamBlock_36[10u];
uint8 MiniNvm_RamBlock_37[11u];
uint8 MiniNvm_RamBlock_38[5u];
uint8 MiniNvm_RamBlock_39[5u];
uint8 MiniNvm_RamBlock_40[8u];
uint8 MiniNvm_RamBlock_41[3u];
uint8 MiniNvm_RamBlock_42[20u];
uint8 MiniNvm_RamBlock_43[5u];
uint8 MiniNvm_RamBlock_44[5u];
uint8 MiniNvm_RamBlock_45[32u];
uint8 MiniNvm_RamBlock_46[5u];
uint8 MiniNvm_RamBlock_47[5u];
uint8 MiniNvm_RamBlock_48[458u];
uint8 MiniNvm_RamBlock_49[1u];
uint8 MiniNvm_RamBlock_50[1u];
uint8 MiniNvm_RamBlock_51[2u];
uint8 MiniNvm_RamBlock_52[1u];
uint8 MiniNvm_RamBlock_53[1u];
uint8 MiniNvm_RamBlock_54[1u];
uint8 MiniNvm_RamBlock_55[112u];
uint8 MiniNvm_RamBlock_56[21u];
uint8 MiniNvm_RamBlock_57[16u];
uint8 MiniNvm_RamBlock_58[880u];

/* 块描述符表（BlockId 1..N，MiniFeeBlockId 0..N-1，长度与 MiniFee 块配置一致） */
const MiniNvm_BlockDescriptorType MiniNvm_BlockDescriptor[MININVM_BLOCK_COUNT] =
{
    { 1u,  MINIFEE_BLOCK_APP_BLOCK_ID,       2u,   MiniNvm_RamBlock_1,  NULL_PTR, NULL_PTR },
    { 2u,  MINIFEE_BLOCK_REPROGRAM_FLAG,     4u,   MiniNvm_RamBlock_2,  NULL_PTR, NULL_PTR },
    { 3u,  MINIFEE_BLOCK_RESET_FLAG,         4u,   MiniNvm_RamBlock_3,  NULL_PTR, NULL_PTR },
    { 4u,  MINIFEE_BLOCK_SECURITY_FLAG,      4u,   MiniNvm_RamBlock_4,  NULL_PTR, NULL_PTR },
    { 5u,  MINIFEE_BLOCK_NEGATIVE_RESP_FLAG, 4u,   MiniNvm_RamBlock_5,  NULL_PTR, NULL_PTR },
    { 6u,  MINIFEE_BLOCK_ALLOW_F187_WRITE,   1u,   MiniNvm_RamBlock_6,  NULL_PTR, NULL_PTR },
    { 7u,  MINIFEE_BLOCK_ALLOW_F190_WRITE,   1u,   MiniNvm_RamBlock_7,  NULL_PTR, NULL_PTR },
    { 8u,  MINIFEE_BLOCK_PROGRAM_COUNT,      2u,   MiniNvm_RamBlock_8,  NULL_PTR, NULL_PTR },
    { 9u,  MINIFEE_BLOCK_DID_F100,           6u,   MiniNvm_RamBlock_9,  NULL_PTR, NULL_PTR },
    { 10u, MINIFEE_BLOCK_DID_F110,           16u,  MiniNvm_RamBlock_10, NULL_PTR, NULL_PTR },
    { 11u, MINIFEE_BLOCK_DID_F111,           16u,  MiniNvm_RamBlock_11, NULL_PTR, NULL_PTR },
    { 12u, MINIFEE_BLOCK_DID_F112,           16u,  MiniNvm_RamBlock_12, NULL_PTR, NULL_PTR },
    { 13u, MINIFEE_BLOCK_DID_F113,           16u,  MiniNvm_RamBlock_13, NULL_PTR, NULL_PTR },
    { 14u, MINIFEE_BLOCK_DID_F114,           16u,  MiniNvm_RamBlock_14, NULL_PTR, NULL_PTR },
    { 15u, MINIFEE_BLOCK_DID_F115,           16u,  MiniNvm_RamBlock_15, NULL_PTR, NULL_PTR },
    { 16u, MINIFEE_BLOCK_DID_F116,           16u,  MiniNvm_RamBlock_16, NULL_PTR, NULL_PTR },
    { 17u, MINIFEE_BLOCK_DID_F117,           16u,  MiniNvm_RamBlock_17, NULL_PTR, NULL_PTR },
    { 18u, MINIFEE_BLOCK_DID_F118,           16u,  MiniNvm_RamBlock_18, NULL_PTR, NULL_PTR },
    { 19u, MINIFEE_BLOCK_DID_F119,           16u,  MiniNvm_RamBlock_19, NULL_PTR, NULL_PTR },
    { 20u, MINIFEE_BLOCK_DID_F11A,           16u,  MiniNvm_RamBlock_20, NULL_PTR, NULL_PTR },
    { 21u, MINIFEE_BLOCK_DID_F11B,           16u,  MiniNvm_RamBlock_21, NULL_PTR, NULL_PTR },
    { 22u, MINIFEE_BLOCK_DID_F11C,           16u,  MiniNvm_RamBlock_22, NULL_PTR, NULL_PTR },
    { 23u, MINIFEE_BLOCK_DID_F11D,           16u,  MiniNvm_RamBlock_23, NULL_PTR, NULL_PTR },
    { 24u, MINIFEE_BLOCK_DID_F11E,           16u,  MiniNvm_RamBlock_24, NULL_PTR, NULL_PTR },
    { 25u, MINIFEE_BLOCK_DID_F11F,           16u,  MiniNvm_RamBlock_25, NULL_PTR, NULL_PTR },
    { 26u, MINIFEE_BLOCK_DID_F120,           16u,  MiniNvm_RamBlock_26, NULL_PTR, NULL_PTR },
    { 27u, MINIFEE_BLOCK_DID_F121,           16u,  MiniNvm_RamBlock_27, NULL_PTR, NULL_PTR },
    { 28u, MINIFEE_BLOCK_DID_F183,           10u,  MiniNvm_RamBlock_28, NULL_PTR, NULL_PTR },
    { 29u, MINIFEE_BLOCK_DID_F187,           5u,   MiniNvm_RamBlock_29, NULL_PTR, NULL_PTR },
    { 30u, MINIFEE_BLOCK_DID_F18A,           5u,   MiniNvm_RamBlock_30, NULL_PTR, NULL_PTR },
    { 31u, MINIFEE_BLOCK_DID_F18B,           3u,   MiniNvm_RamBlock_31, NULL_PTR, NULL_PTR },
    { 32u, MINIFEE_BLOCK_DID_F18C,           16u,  MiniNvm_RamBlock_32, NULL_PTR, NULL_PTR },
    { 33u, MINIFEE_BLOCK_DID_F190,           17u,  MiniNvm_RamBlock_33, NULL_PTR, NULL_PTR },
    { 34u, MINIFEE_BLOCK_DID_F191,           5u,   MiniNvm_RamBlock_34, NULL_PTR, NULL_PTR },
    { 35u, MINIFEE_BLOCK_DID_F192,           10u,  MiniNvm_RamBlock_35, NULL_PTR, NULL_PTR },
    { 36u, MINIFEE_BLOCK_DID_F194,           10u,  MiniNvm_RamBlock_36, NULL_PTR, NULL_PTR },
    { 37u, MINIFEE_BLOCK_DID_F198,           11u,  MiniNvm_RamBlock_37, NULL_PTR, NULL_PTR },
    { 38u, MINIFEE_BLOCK_DID_F1A0,           5u,   MiniNvm_RamBlock_38, NULL_PTR, NULL_PTR },
    { 39u, MINIFEE_BLOCK_DID_F1A1,           5u,   MiniNvm_RamBlock_39, NULL_PTR, NULL_PTR },
    { 40u, MINIFEE_BLOCK_DID_F1A2,           8u,   MiniNvm_RamBlock_40, NULL_PTR, NULL_PTR },
    { 41u, MINIFEE_BLOCK_DID_F1A5,           3u,   MiniNvm_RamBlock_41, NULL_PTR, NULL_PTR },
    { 42u, MINIFEE_BLOCK_DID_F1A8,           20u,  MiniNvm_RamBlock_42, NULL_PTR, NULL_PTR },
    { 43u, MINIFEE_BLOCK_DID_F1A9,           5u,   MiniNvm_RamBlock_43, NULL_PTR, NULL_PTR },
    { 44u, MINIFEE_BLOCK_DID_F1AA,           5u,   MiniNvm_RamBlock_44, NULL_PTR, NULL_PTR },
    { 45u, MINIFEE_BLOCK_DID_F130,           32u,  MiniNvm_RamBlock_45, NULL_PTR, NULL_PTR },
    { 46u, MINIFEE_BLOCK_DID_F1B5,           5u,   MiniNvm_RamBlock_46, NULL_PTR, NULL_PTR },
    { 47u, MINIFEE_BLOCK_DID_F1B6,           5u,   MiniNvm_RamBlock_47, NULL_PTR, NULL_PTR },
    { 48u, MINIFEE_BLOCK_DID_AFF1,           458u, MiniNvm_RamBlock_48, NULL_PTR, NULL_PTR },
    { 49u, MINIFEE_BLOCK_DID_AFF2,           1u,   MiniNvm_RamBlock_49, NULL_PTR, NULL_PTR },
    { 50u, MINIFEE_BLOCK_DID_AFF5,           1u,   MiniNvm_RamBlock_50, NULL_PTR, NULL_PTR },
    { 51u, MINIFEE_BLOCK_DID_AFFC,           2u,   MiniNvm_RamBlock_51, NULL_PTR, NULL_PTR },
    { 52u, MINIFEE_BLOCK_DID_AFFD,           1u,   MiniNvm_RamBlock_52, NULL_PTR, NULL_PTR },
    { 53u, MINIFEE_BLOCK_DID_AFFE,           1u,   MiniNvm_RamBlock_53, NULL_PTR, NULL_PTR },
    { 54u, MINIFEE_BLOCK_DID_AFFF,           1u,   MiniNvm_RamBlock_54, NULL_PTR, NULL_PTR },
    { 55u, MINIFEE_BLOCK_DID_A333,           112u, MiniNvm_RamBlock_55, NULL_PTR, NULL_PTR },
    { 56u, MINIFEE_BLOCK_DID_A444,           21u,  MiniNvm_RamBlock_56, NULL_PTR, NULL_PTR },
    { 57u, MINIFEE_BLOCK_DID_A555,           16u,  MiniNvm_RamBlock_57, NULL_PTR, NULL_PTR },
    { 58u, MINIFEE_BLOCK_VSS_DATA,           880u, MiniNvm_RamBlock_58, NULL_PTR, NULL_PTR }
};