# MiniFee 软件详细设计

| 项目 | 内容 |
| --- | --- |
| 模块名称 | MiniFee |
| 所属层级 | BSW / SystemServices / BootServices |
| 上层模块 | MiniNvm / 直接调用方（vss、SecureBoot 等） |
| 下层模块 | MiniFlsIf |
| 设计版本 | V1.0 |
| 作者 | CaoLiang |
| 描述 | 初版发布：基于 MiniFlsIf 的异步非易失存储抽象层 |

## 1. 模块概述

MiniFee 是基于 MiniFlsIf 的非易失存储抽象层，类似精简版 EEPROM 仿真（FEE）。它以逻辑 Block 为管理单位，把上层逻辑数据组织为 Flash 记录，负责实际写入、完整性校验、追加式存储、簇轮换（迁移）和磨损均衡。上层不直接访问 Flash，也不感知物理布局。

MiniFee 完全异步：`MiniFee_Read` / `MiniFee_Write` 立即返回，调用方必须周期调用 `MiniFee_MainFunction` 推进状态机，并通过 `MiniFee_GetStatus` 查询最终结果。

## 2. 软件架构与依赖

```text
MiniNvm / 上层模块
    | MiniFee 异步 Block 接口
    v
MiniFee：Block、记录、CRC 校验、迁移、磨损均衡
    | MiniFlsIf
    v
MiniFlsIf：平台适配
    |
    v
Flash 驱动
```

| 依赖 | 说明 |
| --- | --- |
| `Common.h` | 基础类型与 `CommF_DataSet` / `CommF_DataCopy` |
| `MemM.h` | 内存管理公共声明 |
| `MiniFee_Cfg.h` | 块配置表、Cluster 配置表、地址基址、块枚举 |
| `MiniFlsIf.h` | 底层异步 Flash 接口 |

## 3. 对外接口

| 接口 | 原型 | 说明 |
| --- | --- | --- |
| `MiniFee_Init` | `void MiniFee_Init(void)` | 初始化 MiniFlsIf 并复位上下文 |
| `MiniFee_DeInit` | `uint8 MiniFee_DeInit(void)` | 反初始化 MiniFlsIf 并复位上下文，固定返回 `E_OK` |
| `MiniFee_Read` | `uint8 MiniFee_Read(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf)` | 发起指定 Block 的异步读 |
| `MiniFee_Write` | `uint8 MiniFee_Write(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf)` | 发起指定 Block 的异步写 |
| `MiniFee_Cancel` | `uint8 MiniFee_Cancel(void)` | 取消当前异步任务 |
| `MiniFee_MainFunction` | `void MiniFee_MainFunction(void)` | 推进状态机（每次只推进有限状态，不同步等待 Flash） |
| `MiniFee_GetStatus` | `MiniFee_StatusType MiniFee_GetStatus(void)` | 查询任务状态 |

**任务状态 `MiniFee_StatusType`：**

| 值 | 含义 |
| --- | --- |
| `MINIFEE_STATUS_IDLE` | 空闲，可接受新请求 |
| `MINIFEE_STATUS_BUSY` | 任务进行中 |
| `MINIFEE_STATUS_OK` | 最近一次任务成功 |
| `MINIFEE_STATUS_NOT_OK` | 最近一次任务失败/被取消 |

**参数约定：**

- `blockNumber` 必须为 `MiniFee_BlockIdType` 枚举值，且 `blockNumber == MiniFee_BlockConfig[blockNumber].BlockNumber`；
- `size` 必须严格等于 `MiniFee_BlockConfig[blockNumber].Length`；
- `buf` 不得为 `NULL_PTR`（`size == 0` 除外，此时直接返回 `E_OK`）；
- 返回 `E_OK` 仅表示请求被接受；最终结果经 `MiniFee_GetStatus` 查询。

## 4. 存储格式

### 4.1 Cluster 头（8 字节，大端）

| 字段 | 偏移 | 长度 | 说明 |
| --- | --- | --- | --- |
| Magic | 0 | 4 | `0xAA55AA44`，识别已初始化区域 |
| Generation | 4 | 3 | 激活代次（大端），每次激活自增；活动簇凭最大 Generation 判定 |
| CheckSum | 7 | 1 | CRC-8（多项式 0x07），输入为前 7 字节 |

**Generation 自增规则**：写簇头前由 MiniFee 读取当前活动簇（源簇）的 Generation 并自动 +1，不依赖外部参数；若源簇读取不到（首次激活/空白）或读取失败（CRC 无效），则从 0 开始（写入 1）。

**写缓冲约束**：底层 `Fls_Write` 为异步接口，仅在调用时保存源缓冲指针，实际数据在后续 `Fls_MainFunction` 中分多次读取。因此簇头写入的源缓冲必须是**持久缓冲**（上下文成员 `writeHdrBuf`），禁止使用栈上临时数组，否则簇头在写入完成前会被后续函数调用复用的栈空间破坏。

### 4.2 记录（8 字节块头 + 数据）

| 字段 | 偏移 | 长度 | 说明 |
| --- | --- | --- | --- |
| BlockNumber | 0 | 1 | 逻辑 Block 号 |
| Length | 1 | 2 | 逻辑数据长度（非物理占用，大端） |
| PrevOffset | 3 | 4 | 本簇上一条记录头偏移（0=首条），供迁移反向扫描（大端） |
| CheckSum | 7 | 1 | CRC-8（多项式 0x07），输入为 `BlockNumber + Length + PrevOffset` 共 7 字节 |

- 数据区物理占用 = `MINIFEE_ALIGN_LEN(Length)`，即按 `MINIFEE_VIRTUALPAGE_SIZE` 向上对齐；真实数据在前 `Length` 字节，其余以 0 填充；
- 记录物理步长 = `MINIFEE_BLOCK_HEADER_SIZE + align(Length)`；
- 块头 + 数据在**同一次 `Fls_Write`** 中写入，块内不存在事后单独编程的标志页（RH850 数据 Flash 两次擦除之间仅允许一次编程）。

### 4.3 CRC-8

多项式 `0x07`，初始值 `0x00`，不反相，逐字节计算。簇头校验输入为前 7 字节；块头校验输入为 `BlockNumber(1) + Length(2, 大端) + PrevOffset(4, 大端)`。

## 5. 状态机设计

### 5.1 主状态 `MiniFee_StateType`

| 状态 | 说明 |
| --- | --- |
| `MINIFEE_STATE_IDLE` | 空闲 |
| `MINIFEE_STATE_FLS_WAIT` | 通用 MiniFlsIf 异步等待 |
| `MINIFEE_STATE_SCAN` | 通用块头扫描 |
| `MINIFEE_STATE_READ_FIND_ADDR` | 读流程：簇选择入口 |
| `MINIFEE_STATE_READ_VERIFY_COPY` | 读流程：拷贝逻辑数据 |
| `MINIFEE_STATE_READ_DONE` | 读流程：收尾 |
| `MINIFEE_STATE_WRITE_FIND_ADDR` | 写流程：簇选择入口 |
| `MINIFEE_STATE_WRITE_CHECK_CMP` | 写流程：与旧数据整块比对 |
| `MINIFEE_STATE_WRITE_MAIN_LOOP_PROC` | 写流程：组装并单次写入块头+数据 |
| `MINIFEE_STATE_WRITE_DONE` | 写流程：收尾 |
| `MINIFEE_STATE_FIND_PROC` | FindAddr / 簇选择子流程 |
| `MINIFEE_STATE_ERROR` | 错误收尾 |

### 5.2 FindAddr 子状态 `MiniFee_FindSubState`

FindAddr 负责“选活动簇 + 空间/健康指标扫描 + 必要时迁移”，是一个可被 `Fls_Wait` 反复打断的异步子状态机：

| 子状态 | 说明 |
| --- | --- |
| `FIND_SUB_START` | 逐簇读取簇头 |
| `FIND_SUB_READ_CLUSTER_HDR_DONE` | 解析簇头，取 CRC 有效且 Magic 匹配者中 Generation 最大者为活动簇 |
| `FIND_SUB_DECIDE` | 决策：无活动簇时，写请求→首次激活簇 0，读请求→失败 |
| `FIND_SUB_FRESH_ERASE` | 首次激活：擦除簇 0 |
| `FIND_SUB_FRESH_ACTIVATE` | 首次激活：写簇 0 簇头 |
| `FIND_SUB_FRESH_ACTIVATE_DONE` | 首次激活收尾：设置活动簇游标并校验空间 |
| `FIND_SUB_METRICS_READ` | 写前扫描：逐条读块头 |
| `FIND_SUB_METRICS_EVAL` | 评估块头：合法则推进 `headerEnd/lastRecordStart`，非法则标记损坏 |
| `FIND_SUB_METRICS_DONE` | 汇总：损坏或空间不足 → 迁移 |
| `FIND_SUB_MIGRATE_PREPARE` | 迁移准备：目标簇=(活动簇+1)%簇数，清空去重位图，从 `lastRecordStart` 反向扫描 |
| `FIND_SUB_MIGRATE_ERASE_DST` | 擦除目标簇 |
| `FIND_SUB_MIGRATE_READ` | 读上一条记录块头 |
| `FIND_SUB_MIGRATE_EVAL` | 校验并按 BlockNumber 位图去重，只迁移每块最新记录 |
| `FIND_SUB_MIGRATE_DATA_READ` | 读取待迁移数据 |
| `FIND_SUB_MIGRATE_WRITE` | 写目标簇新记录，并更新目标簇内 `PrevOffset` 链 |
| `FIND_SUB_MIGRATE_ACTIVATE` | 写目标簇簇头（Generation 源簇+1） |
| `FIND_SUB_MIGRATE_SWITCH` | 切换活动簇到目标簇（老簇保留） |
| `FIND_SUB_WRITE_HDR_READ` | 写簇头子流程：读源簇 Generation |
| `FIND_SUB_WRITE_HDR_WRITE` | 写簇头子流程：写目标簇头 |
| `FIND_SUB_DONE` | 子流程结束，返回主状态 |

### 5.3 读流程

1. `MiniFee_Read` 校验参数，置 `BUSY`，进入 `READ_FIND_ADDR`，启动 FindAddr（`FIND_TYPE_READ`）；
2. FindAddr 选定活动簇；若无有效簇则失败；
3. `StartBlockScan` 从簇头之后逐条扫描记录，定位目标 Block 的**最新**有效记录；
4. 找到则读取数据并只复制 `logicalBlockSize` 到上层缓冲；未找到则 `NOT_OK`；
5. `READ_DONE` → `FinalizeOk`。

### 5.4 写流程

1. `MiniFee_Write` 校验参数，置 `BUSY`，进入 `WRITE_FIND_ADDR`，启动 FindAddr（`FIND_TYPE_WRITE`）；
2. FindAddr 选定活动簇，扫描记录计算 `headerEnd`（写入游标）、`lastRecordStart`（上一条记录），必要时迁移；
3. `StartBlockScan` 找到该 Block 的最新记录并读取旧数据；
4. `WRITE_CHECK_CMP`：与待写数据整块比对，完全一致则跳过写入（`WRITE_DONE`）；
5. 否则 `WRITE_MAIN_LOOP_PROC`：组装块头（`PrevOffset = lastRecordStart`，`Length = logicalBlockSize`，CRC-8）与对齐后的数据，**单次**写入活动簇 `headerEnd` 处；更新 `lastRecordStart/headerEnd`；
6. `WRITE_DONE` → `FinalizeOk`。

### 5.5 迁移与磨损均衡

- 触发条件：活动簇存在损坏记录，或剩余空间不足以容纳本次写入；
- 目标簇 = `(activeClusterIndex + 1) % MINIFEE_CLUSTER_COUNT`；
- **仅在写入前擦除目标簇**；源簇内容保留，断电/异常时可回退，源簇待下次作为迁移目标时再擦除；
- 从 `lastRecordStart` 开始沿 `PrevOffset` **反向扫描**（最新→最旧），用位图按 BlockNumber 去重，每块只迁移最新一份；
- 迁移记录写入目标簇时重新生成 `PrevOffset` 链（指向目标簇内已迁移的上一条）；
- 迁移完成后写目标簇头（Generation = 源簇 Generation + 1），切换活动簇；源簇头保持原状，不二次改写。

### 5.6 通用块扫描 `HandleBlockScan`

- 从 `MINIFEE_CLUSTER_HEADER_SIZE` 开始逐条读块头；
- 全 `0xFF` 表示到达簇尾空白，结束；
- 块头 CRC 失败或长度非法视为损坏，结束扫描（并可使迁移流程介入）；
- `findLatest != 0` 时记录最后一次匹配 BlockNumber 的记录位置与长度；
- 需要数据时再读取该记录数据到 `blockDataBuf`；未找到且 `fillBlank != 0` 时用 `0xFF` 填充（供写流程比对）。

## 6. 错误处理与约束

- Busy 时拒绝新请求；非法 Block、长度不匹配或空指针返回 `E_NOT_OK`；
- 底层 Pending 时保持上下文，不重复发起请求；底层失败、取消、记录损坏或迁移失败返回失败状态；
- 不提供字节偏移访问、Dataset、冗余 Block 或上层 RAM 管理；
- 所有底层访问经 MiniFlsIf，不直接依赖具体 Flash 驱动；
- 任何区域仅一次编程：不写 Valid/Invalid 标志，不进行 1→0 二次编程。

## 7. 关键注意事项

1. **持久写缓冲**：`writeBuf`、`writeHdrBuf` 是上下文成员，因 `Fls_Write` 异步持有源指针，禁止改用栈上临时数组；
2. **逻辑长度 vs 物理长度**：`logicalBlockSize` 为上层数据长度，`blockSize` 为按 `MINIFEE_VIRTUALPAGE_SIZE` 对齐后的物理长度；上层只会看到逻辑长度；
3. **PrevOffset 链**：迁移反向扫描依赖它；新增记录时 `PrevOffset` 必须指向当前簇内上一条记录（`lastRecordStart`）；
4. **簇数量须 ≥ 2**：迁移采用双簇轮换；`MINIFEE_CLUSTER_COUNT` 必须与 `MiniFee_ClusterConfig[]` 条目数一致；
5. **地址对齐**：Cluster `StartAddress` 须与 Fls 擦除单元对齐，`Length` 须为擦除单元整数倍；
6. **缓冲区容量**：`MINIFEE_MAX_BLOCK_DATA_SIZE` 须 ≥ 所有块 `Length` 的最大值；
7. **配置一致性**：`MiniFee_BlockConfig[i].BlockNumber` 必须等于 `i`；`MiniFee_BlockIdType` 哨兵 `MINIFEE_BLOCK_MAX` 自动决定 `MINIFEE_BLOCK_COUNT`。

## 8. 验证要求

- 单 Block 异步读写与状态映射；
- 数据不变时跳过写入；
- 记录校验失败处理；
- 簇迁移/翻页及断电回退（老簇保留）；
- 多次整块重写触发迁移后数据保全与 Generation 递增；
- Flash 写入粒度与物理对齐正确处理。