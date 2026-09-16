# MiniNvm 软件详细设计

| 项目 | 内容 |
| --- | --- |
| 模块名称 | MiniNvm |
| 所属层级 | BSW / SystemServices / BootServices |
| 上层模块 | BootServices 应用（EcuMService、vss、SecureBoot 等） |
| 下层模块 | MiniFee |
| 设计版本 | V1.1 |
| 作者 | Manco |
| 描述 | 初版发布：轻量级非易失数据管理与异步请求队列 |

## 1. 模块概述

MiniNvm 是 BootServices 的轻量级非易失数据管理模块，类似精简版 NvM。它管理每个逻辑 Block 的 RAM 缓冲区，排队单块/多块异步请求并维护请求结果。所有实际 Flash 操作由 MiniFee 完成，MiniNvm 不处理物理布局、对齐和底层写入细节。

MiniNvm 完全异步：`ReadBlock` / `WriteBlock` / `ReadAll` / `WriteAll` 立即返回，调用方必须周期调用 `MiniNvm_MainFunction`，并通过 `MiniNvm_GetErrorStatus` / `MiniNvm_GetMultiJobStatus` 查询最终结果。

除异步接口外，MiniNvm 提供同步 RAM 访问接口（`GetRamBuffer` / `ReadRam` / `WriteRam`）：运行期上层只读写各 Block 的 RAM buffer，**仅当数据相对原值实际变化时才置对应脏标记**；下电 / 复位 / 跳转前调用 `MiniNvm_WriteAll`，仅将脏 Block 写回非易失存储。该机制避免运行期频繁触发 Flash 作业，并缩短下电保存窗口。

## 2. 软件架构与依赖

```text
BootServices 应用
    | MiniNvm API
    v
MiniNvm：RAM buffer、队列、作业和结果
    | MiniFee API
    v
MiniFee：Flash 存储、校验、迁移和磨损均衡
    |
    v
MiniFlsIf / Flash 驱动
```

| 依赖 | 说明 |
| --- | --- |
| `Common.h` | 基础类型与 `NULL_PTR`、`E_OK` / `E_NOT_OK` |
| `MiniFee.h` | 底层存储接口与 `MiniFee_StatusType` |
| `MiniNvm_Cfg.h` | 块数量、队列容量、块描述符与 RAM buffer 声明 |

## 3. 配置设计

### 3.1 配置宏（`MiniNvm_Cfg.h`）

| 宏 | 性质 | 来源 / 取值依据 |
| --- | --- | --- |
| `MININVM_BLOCK_COUNT` | 由配置推导 | = MiniFee 块总数（`MINIFEE_BLOCK_MAX`），随 MiniFee 块表自动变化，**不得单独修改** |
| `MININVM_MAX_BLOCK_LENGTH` | 由配置推导 | = `MINIFEE_MAX_BLOCK_DATA_SIZE`，共享数据缓冲 `DataBuf` 按此定容，随 MiniFee 配置变化 |
| `MININVM_QUEUE_SIZE` | **用户自定义** | 单块请求队列容量。**与块数、块长等其它参数没有必然关系**，只需 ≥ 上层同一时刻可能提交且尚未处理的单块 `ReadBlock`/`WriteBlock` 请求数；`ReadAll`/`WriteAll` 是一次性作业、不经队列，规模再大也不占队列。取值越大越能吸收突发请求（队满时 `MiniNvm_Enqueue` 返回 `E_NOT_OK`），但每个队列条目占一份元数据大小的 RAM，按最坏并发数取小值即可 |

### 3.2 块描述符 `MiniNvm_BlockDescriptorType`

| 字段 | 说明 |
| --- | --- |
| `BlockId` | MiniNvm 逻辑块号，从 1 开始，须等于数组下标 +1 |
| `MiniFeeBlockId` | 对应的 MiniFee 块号（从 0 开始），须等于数组下标 |
| `Length` | 逻辑数据长度，须等于对应 `MiniFee_BlockConfig[].Length` |
| `RamBlockAddress` | 该块独立 RAM 缓冲区地址，不可为空 |

- 块数量、顺序与长度必须与 MiniFee 块配置一一对应；
- `MiniNvm_Init` 会调用 `MiniNvm_ValidateConfig` 逐项校验上述约束，任一不满足则模块不可用，拒绝所有请求；
- MiniNvm 只以逻辑长度调用 MiniFee，物理对齐由 MiniFee 负责。

## 4. 对外接口

| 接口 | 原型 | 说明 |
| --- | --- | --- |
| `MiniNvm_Init` | `void MiniNvm_Init(void)` | 初始化 MiniFee、校验配置、复位队列与结果 |
| `MiniNvm_DeInit` | `void MiniNvm_DeInit(void)` | 复位为未初始化状态，清空队列与结果 |
| `MiniNvm_ReadBlock` | `uint8 MiniNvm_ReadBlock(uint8 blockId, uint8* dstPtr)` | 请求异步读取指定 Block 到 `dstPtr` |
| `MiniNvm_WriteBlock` | `uint8 MiniNvm_WriteBlock(uint8 blockId, const uint8* srcPtr)` | 请求异步写入指定 Block |
| `MiniNvm_ReadAll` | `uint8 MiniNvm_ReadAll(void)` | 一次性读取全部 Block 到各自 RAM buffer（MiniFee 单遍扫描活动簇） |
| `MiniNvm_WriteAll` | `uint8 MiniNvm_WriteAll(void)` | 一次性写入脏 Block 的 RAM buffer（MiniFee 单遍选簇后连续追加；无脏块时不写入） |
| `MiniNvm_CancelJobs` | `uint8 MiniNvm_CancelJobs(void)` | 取消当前作业与队列，相关结果置失败 |
| `MiniNvm_GetErrorStatus` | `uint8 MiniNvm_GetErrorStatus(uint8 blockId, MiniNvm_RequestResultType* resultPtr)` | 查询单块结果 |
| `MiniNvm_GetMultiJobStatus` | `MiniNvm_RequestResultType MiniNvm_GetMultiJobStatus(void)` | 查询多块（ReadAll/WriteAll）总体结果 |
| `MiniNvm_MainFunction` | `void MiniNvm_MainFunction(void)` | 推进队列与 MiniFee 作业 |
| `MiniNvm_GetRamBuffer` | `uint8* MiniNvm_GetRamBuffer(MiniFee_BlockIdType blockNumber)` | 获取指定块的 RAM buffer 地址（同步，不触发 Flash 作业） |
| `MiniNvm_ReadRam` | `uint8 MiniNvm_ReadRam(MiniFee_BlockIdType blockNumber, uint8* dstPtr, uint32 length)` | 从指定块的 RAM buffer 同步读取（不触发 Flash 作业） |
| `MiniNvm_WriteRam` | `uint8 MiniNvm_WriteRam(MiniFee_BlockIdType blockNumber, const uint8* srcPtr, uint32 length)` | 同步写入指定块的 RAM buffer，并将该块标记为脏 |

**同步 RAM 访问接口**（`GetRamBuffer` / `ReadRam` / `WriteRam`）使用 MiniFee 块号（从 0 开始），运行期上层只读写 RAM buffer，不直接触发 Flash 作业。持久化由 `MiniNvm_ReadAll`（初始化）与 `MiniNvm_WriteAll`（下电 / 复位 / 跳转前）完成。

**请求结果 `MiniNvm_RequestResultType`：**

| 值 | 含义 |
| --- | --- |
| `MININVM_REQ_IDLE` | 无请求/空闲 |
| `MININVM_REQ_PENDING` | 请求进行中 |
| `MININVM_REQ_OK` | 成功 |
| `MININVM_REQ_NOT_OK` | 失败或被取消 |

API 返回 `E_OK` 仅表示请求已接受，最终结果经状态查询接口获取。

## 5. 内部设计

### 5.1 上下文与队列

| 成员 | 说明 |
| --- | --- |
| `Initialized` | 初始化/配置校验通过标志（0=不可用，拒绝一切请求） |
| `CurrentValid` / `CurrentOperation` / `CurrentBlockId` / `CurrentTargetAddress` | 当前作业上下文 |
| `DataBuf[MININVM_MAX_BLOCK_LENGTH]` | 共享数据缓冲：单块作业读/写数据，唯一一份最大块长缓冲 |
| `Queue[MININVM_QUEUE_SIZE]` / `QueueHead` / `QueueTail` / `QueueCount` | 单块请求环形队列（条目仅含元数据，容量 `MININVM_QUEUE_SIZE` 由移植者按并发需求取值） |
| `BlockResult[MININVM_BLOCK_COUNT]` | 每块最近一次结果 |
| `Dirty[MININVM_BLOCK_COUNT]` | 脏块标记：仅当 RAM 数据相对原值实际变化时置位（`WriteRam` / `WriteBlock`） |
| `MultiResult` | 多块作业总体结果 |

队列条目 `MiniNvm_QueueEntryType` 仅携带单块请求元数据（操作类型、BlockId、目标地址），不持有数据缓冲；单块读/写数据统一使用上下文共享缓冲 `DataBuf`。

### 5.2 调度

`MiniNvm_Init` 配置校验通过后置 `Initialized=1`，`MiniNvm_DeInit` 清 0；为 0 时拒绝一切请求。

`MiniNvm_MainFunction` 逻辑：

1. `Initialized == 0` 直接返回；
2. 有当前作业：读取 `MiniFee_GetStatus()` 并处理结果；
3. 无当前作业且队列非空：从队首启动一个作业。

### 5.3 单块读 / 写

**`MiniNvm_ReadBlock`：**

1. 校验初始化状态、目标指针、是否已有同块挂起请求（块号合法性由入队校验）；
2. 入队（结果置 `PENDING`）；
3. 启动时调用 `MiniFee_Read(MiniFeeBlockId, Length, DataBuf)`；
4. 成功后将 `DataBuf` 复制到调用方 `dstPtr`，结果置 `OK`；Flash 中无该块记录时 MiniFee 返回全零，此处同样置 `OK`；
5. 底层失败时置 `NOT_OK`。

**`MiniNvm_WriteBlock`：**

1. 同样的参数与挂起校验；
2. 与当前 RAM buffer 比对：**一致则视为无变化，直接返回 `E_OK`（不置脏、不入队、结果保持不变）**——内容去重在此完成；
3. 不一致则置脏（`Dirty` 置位）并入队：入队即把 `srcPtr` 数据快照进共享 `DataBuf`，入队后调用方可复用源缓冲；
4. 出队启动时调用 `MiniFee_Write(MiniFeeBlockId, Length, DataBuf)`；
5. 成功置 `OK` 并清除脏标记，失败置 `NOT_OK` 且保留脏标记。

### 5.4 ReadAll / WriteAll

二者均为**一次性批量作业，不经队列**（只有单块请求走队列）：

**ReadAll**：先清零全部 RAM buffer（无记录块保持默认全零），再调 `MiniFee_ReadAll` 单遍扫描活动簇、逐条记录经回调填入对应块 RAM buffer；完成后全部 `BlockResult` 与 `MultiResult` 统一置位。

**WriteAll**：先统计有无脏块（无则 `MultiResult` 直接置 `OK`），再调 `MiniFee_WriteAll`——单遍选簇（剩余空间不足待写总量则一次性迁移），随后按块号连续追加全部脏块记录；成功清脏并置 `OK`，失败保留脏并置 `NOT_OK`。写源为各块 RAM buffer。

- 相比逐块 `ReadBlock`/`WriteBlock`，避免逐块重复选簇/扫描，效率更高；
- 进行中通过 `MiniNvm_IsBlockPending` 拒绝新的单块/全量请求；
- 多块作业期间不允许并发启动另一多块作业。

### 5.5 同步 RAM 访问与脏块持久化

- `MiniNvm_GetRamBuffer` / `MiniNvm_ReadRam` / `MiniNvm_WriteRam` 直接操作各块 RAM buffer，不触发 Flash 作业，供运行期上层（如 `2E` 写入 DID、VSS、SecureBoot）使用；
- **仅 `MiniNvm_WriteRam` 会按需置脏**；直接经 `MiniNvm_GetRamBuffer` 改写 RAM buffer 不会置脏，此类块不会被 `MiniNvm_WriteAll` 落盘，运行期修改必须走 `MiniNvm_WriteRam`；
- `MiniNvm_WriteRam` 仅当写入内容相对原数据变化时才复制并置脏；与原数据一致则不做改动（不复制、不置脏）；上层无需再显式调用 `MiniNvm_WriteBlock`；
- 下电 / 复位 / 跳转前调用 `MiniNvm_WriteAll`，仅将脏块一次性写回非易失存储，成功写回后清除对应脏标记；写失败则保留脏标记，便于后续重试；
- `MiniNvm_Init` / `MiniNvm_DeInit` 会将全部脏标记清零。

### 5.6 取消

- `MiniNvm_CancelJobs`：若 MiniFee 正 `BUSY` 则调用 `MiniFee_Cancel`；当前作业与队列中所有请求结果置 `NOT_OK`，清空队列，`MultiResult` 置 `NOT_OK`。

## 6. 周期调度

`MiniNvm_MainFunction` 从队列取请求、启动或检查 MiniFee 作业、更新并收尾结果。

**MiniNvm 不调用 `MiniFee_MainFunction`。** 系统必须按顺序独立调度：

```text
MiniFlsIf_MainFunction  →  MiniFee_MainFunction  →  MiniNvm_MainFunction
```

测试驱动同样显式按此顺序调用。

## 7. 错误处理与约束

- 未初始化时拒绝服务；非法 Block、空指针、重复挂起请求返回 `E_NOT_OK`；
- 队列满时入队失败返回 `E_NOT_OK`；
- Pending 时不得重复启动同一底层作业；
- 取消后相关结果置失败；
- MiniNvm 不修改 MiniFee 数据格式与存储策略。

## 8. 关键注意事项

1. **BlockId 基准差异**：MiniNvm 的 `BlockId` 从 **1** 开始，MiniFee 的块号从 **0** 开始，映射关系由描述符的 `MiniFeeBlockId` 维护；
2. **配置一致性**：描述符数量、顺序、长度必须与 MiniFee 块配置严格对应，否则 `MiniNvm_Init` 校验失败、模块不可用；
3. **写数据快照**：单块 `WriteBlock` 入队即快照进共享 `DataBuf`（入队后源缓冲可复用）；`WriteAll` 源为各块 RAM buffer，出队启动时才快照进 `DataBuf`；
4. **调度顺序**：必须保证先推进 MiniFlsIf、再 MiniFee、最后 MiniNvm，否则结果无法收敛；
5. **队列容量（用户自定义参数）**：`MININVM_QUEUE_SIZE` 与其它配置**没有必然关系**，它只约束“同一时刻排队等待处理的单块请求数”（`ReadAll`/`WriteAll` 是一次性作业，不经队列）；按上层最坏并发单块请求数取值即可，队满时 `MiniNvm_Enqueue` 返回 `E_NOT_OK`、该块结果不变；
6. **内存占用**：队列条目仅含元数据，读/写数据共用上下文单一 `DataBuf[MININVM_MAX_BLOCK_LENGTH]`，RAM 开销 ≈ `MININVM_QUEUE_SIZE × sizeof(队列条目) + MININVM_MAX_BLOCK_LENGTH + MININVM_BLOCK_COUNT × (结果枚举 + 脏标记)`，随块数与队列容量变化；
7. **脏块持久化**：`MiniNvm_WriteRam` / `MiniNvm_WriteBlock` 写入后需通过 `MiniNvm_WriteAll` 落盘；下电 / 复位 / 跳转前必须调用 `MiniNvm_WriteAll`，否则运行期修改仅存在于 RAM，复位后丢失；
8. **脏标记生命周期**：仅当数据相对原值实际变化时置位；写成功清除、写失败保留、`Init`/`DeInit` 清零；无脏块时 `WriteAll` 不写入。

## 9. 验证要求

- 单块读写、异步结果与状态映射；
- ReadAll / WriteAll 与各 RAM buffer 数据一致性（覆盖全部 Block）；
- 队列、重复请求、取消；
- 非法参数处理；
- 同步 RAM 访问（`GetRamBuffer` / `ReadRam` / `WriteRam`）与脏块标记：`WriteRam` 数据变化时置脏、一致时不置脏，`WriteAll` 仅写脏块，写成功清标记、写失败保留标记；
- 单块写去重：`WriteBlock` 数据与当前 RAM 一致时不入队、不置脏（内容去重）；
- 一次性读/写（`ReadAll`/`WriteAll`）整块数据一致性与 `MultiResult` 置位；
- 空白 Flash 或 Block 无记录时：读取返回全零默认值并置 `OK`；
- 下电 / 复位保存链路：`ResetService_EcuReset` → `EcuMService_StoreAllParams` → `MiniNvm_WriteAll` 后数据落盘，复位后可读回；
- 簇迁移 / 翻页与簇头（CRC-8 + Magic + Generation）校验；
- 长度非对齐数据由 MiniFee 处理，MiniNvm 不参与。
