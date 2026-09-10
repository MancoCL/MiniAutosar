# MiniNvm 软件详细设计

| 项目 | 内容 |
| --- | --- |
| 模块名称 | MiniNvm |
| 所属层级 | BSW / SystemServices / BootServices |
| 上层模块 | BootServices 应用（EcuMService、vss、SecureBoot 等） |
| 下层模块 | MiniFee |
| 设计版本 | V1.0 |
| 作者 | CaoLiang |
| 描述 | 初版发布：轻量级非易失数据管理与异步请求队列 |

## 1. 模块概述

MiniNvm 是 BootServices 的轻量级非易失数据管理模块，类似精简版 NvM。它管理每个逻辑 Block 的 RAM 缓冲区，排队单块/多块异步请求并维护请求结果。所有实际 Flash 操作由 MiniFee 完成，MiniNvm 不处理物理布局、对齐和底层写入细节。

MiniNvm 完全异步：`ReadBlock` / `WriteBlock` / `ReadAll` / `WriteAll` 立即返回，调用方必须周期调用 `MiniNvm_MainFunction`，并通过 `MiniNvm_GetErrorStatus` / `MiniNvm_GetMultiJobStatus` 查询最终结果。

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

| 宏 | 值/来源 | 说明 |
| --- | --- | --- |
| `MININVM_BLOCK_COUNT` | `MINIFEE_BLOCK_MAX` | 逻辑 Block 数量，跟随 MiniFee 块配置 |
| `MININVM_MAX_BLOCK_LENGTH` | `MINIFEE_MAX_BLOCK_DATA_SIZE` | 单块最大长度，内部队列条目按此定容 |
| `MININVM_QUEUE_SIZE` | `MININVM_BLOCK_COUNT` | 环形请求队列容量，保证可一次排入全部 Block |

### 3.2 块描述符 `MiniNvm_BlockDescriptorType`

| 字段 | 说明 |
| --- | --- |
| `BlockId` | MiniNvm 逻辑块号，从 1 开始，须等于数组下标 +1 |
| `MiniFeeBlockId` | 对应的 MiniFee 块号（从 0 开始），须等于数组下标 |
| `Length` | 逻辑数据长度，须等于对应 `MiniFee_BlockConfig[].Length` |
| `RamBlockAddress` | 该块独立 RAM 缓冲区地址，不可为空 |
| `RomBlockAddress` | 可选：ROM 默认值地址，用于读取失败时恢复 |
| `InitBlockCallback` | 可选：默认值初始化回调（无 ROM 时使用） |

- 块数量、顺序与长度必须与 MiniFee 块配置一一对应；
- `MiniNvm_Init` 会调用 `MiniNvm_ValidateConfig` 逐项校验上述约束，任一不满足则模块保持 `UNINIT`，拒绝所有请求；
- MiniNvm 只以逻辑长度调用 MiniFee，物理对齐由 MiniFee 负责。

## 4. 对外接口

| 接口 | 原型 | 说明 |
| --- | --- | --- |
| `MiniNvm_Init` | `void MiniNvm_Init(void)` | 初始化 MiniFee、校验配置、复位队列与结果 |
| `MiniNvm_DeInit` | `void MiniNvm_DeInit(void)` | 复位为未初始化状态，清空队列与结果 |
| `MiniNvm_ReadBlock` | `uint8 MiniNvm_ReadBlock(uint8 blockId, uint8* dstPtr)` | 请求异步读取指定 Block 到 `dstPtr` |
| `MiniNvm_WriteBlock` | `uint8 MiniNvm_WriteBlock(uint8 blockId, const uint8* srcPtr)` | 请求异步写入指定 Block |
| `MiniNvm_ReadAll` | `uint8 MiniNvm_ReadAll(void)` | 按配置顺序读取全部 Block 到各自 RAM buffer |
| `MiniNvm_WriteAll` | `uint8 MiniNvm_WriteAll(void)` | 按配置顺序写入全部 RAM buffer |
| `MiniNvm_CancelJobs` | `uint8 MiniNvm_CancelJobs(void)` | 取消当前作业与队列，相关结果置失败 |
| `MiniNvm_RestoreBlockDefaults` | `uint8 MiniNvm_RestoreBlockDefaults(uint8 blockId)` | 用 ROM 数据或回调恢复默认值 |
| `MiniNvm_GetErrorStatus` | `uint8 MiniNvm_GetErrorStatus(uint8 blockId, MiniNvm_RequestResultType* resultPtr)` | 查询单块结果 |
| `MiniNvm_GetMultiJobStatus` | `MiniNvm_RequestResultType MiniNvm_GetMultiJobStatus(void)` | 查询多块（ReadAll/WriteAll）总体结果 |
| `MiniNvm_MainFunction` | `void MiniNvm_MainFunction(void)` | 推进队列与 MiniFee 作业 |

**请求结果 `MiniNvm_RequestResultType`：**

| 值 | 含义 |
| --- | --- |
| `MININVM_REQ_IDLE` | 无请求/空闲 |
| `MININVM_REQ_PENDING` | 请求进行中 |
| `MININVM_REQ_OK` | 成功 |
| `MININVM_REQ_NOT_OK` | 失败或被取消 |
| `MININVM_REQ_RESTORED_FROM_ROM` | 读取失败但已用 ROM/回调恢复默认值 |

API 返回 `E_OK` 仅表示请求已接受，最终结果经状态查询接口获取。

## 5. 内部设计

### 5.1 上下文与队列

| 成员 | 说明 |
| --- | --- |
| `State` | 模块状态：`UNINIT` / `IDLE` / `WAIT_MINIFEE` |
| `ConfigValid` | 配置校验结果 |
| `CurrentValid` / `CurrentOperation` / `CurrentBlockId` / `CurrentTargetAddress` / `CurrentIsMulti` / `CurrentEntry` | 当前作业上下文 |
| `Queue[MININVM_QUEUE_SIZE]` / `QueueHead` / `QueueTail` / `QueueCount` | 环形请求队列 |
| `BlockResult[MININVM_BLOCK_COUNT]` | 每块最近一次结果 |
| `MultiResult` | 多块总体结果 |

队列条目 `MiniNvm_QueueEntryType` 携带操作类型、BlockId、目标地址、是否多块，以及数据缓冲 `Data[MININVM_MAX_BLOCK_LENGTH]`。

### 5.2 状态流转

```text
UNINIT ──MiniNvm_Init（配置有效）──> IDLE
IDLE   ──取队列启动 MiniFee 作业──> WAIT_MINIFEE
WAIT_MINIFEE ──MiniFee 完成/失败──> IDLE（结果写入 BlockResult/MultiResult）
任意状态 ──MiniNvm_DeInit──> UNINIT
```

`MiniNvm_MainFunction` 逻辑：

1. `UNINIT` 直接返回；
2. 有当前作业：读取 `MiniFee_GetStatus()` 并处理结果；
3. 无当前作业且队列非空：从队首启动一个作业。

### 5.3 单块读 / 写

**`MiniNvm_ReadBlock`：**

1. 校验初始化状态、BlockId、目标指针、是否已有同块挂起请求；
2. 入队（结果置 `PENDING`）；
3. 启动时调用 `MiniFee_Read(MiniFeeBlockId, Length, entry.Data)`；
4. 成功后将 `entry.Data` 复制到调用方 `dstPtr`，结果置 `OK`；
5. 失败时尝试 `MiniNvm_RestoreCurrent`（ROM 或回调），成功置 `RESTORED_FROM_ROM`，否则 `NOT_OK`。

**`MiniNvm_WriteBlock`：**

1. 同样的参数与挂起校验；
2. **入队时立即快照** `srcPtr` 指向的数据到 `entry.Data`，因此入队后调用方即可复用源缓冲；
3. 启动时调用 `MiniFee_Write(MiniFeeBlockId, Length, entry.Data)`；
4. 成功置 `OK`，失败置 `NOT_OK`（写失败不做默认值恢复）。

### 5.4 ReadAll / WriteAll

- 二者都调用 `MiniNvm_StartAll`：当前无作业且队列为空时，按配置顺序把全部 Block 入队，`MultiResult` 置 `PENDING`；
- 任一子作业失败则 `MultiResult` 置 `NOT_OK`；全部成功且队列清空后置 `OK`；
- 多块作业期间不允许并发启动另一多块作业。

### 5.5 取消与默认值恢复

- `MiniNvm_CancelJobs`：若 MiniFee 正 `BUSY` 则调用 `MiniFee_Cancel`；当前作业与队列中所有请求结果置 `NOT_OK`，清空队列，`MultiResult` 置 `NOT_OK`；
- `MiniNvm_RestoreBlockDefaults`：优先从 `RomBlockAddress` 复制；无 ROM 时调用 `InitBlockCallback`；都不可用则失败。结果置 `RESTORED_FROM_ROM` 或 `NOT_OK`，并同步写入 `BlockResult`。

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
- MiniNvm 不修改 MiniFee 数据格式与存储策略；默认值恢复只用 ROM 数据或初始化回调。

## 8. 关键注意事项

1. **BlockId 基准差异**：MiniNvm 的 `BlockId` 从 **1** 开始，MiniFee 的块号从 **0** 开始，映射关系由描述符的 `MiniFeeBlockId` 维护；
2. **配置一致性**：描述符数量、顺序、长度必须与 MiniFee 块配置严格对应，否则 `MiniNvm_Init` 校验失败、模块不可用；
3. **写数据快照**：`WriteBlock` 入队即复制数据，避免异步执行期间源缓冲被修改；
4. **调度顺序**：必须保证先推进 MiniFlsIf、再 MiniFee、最后 MiniNvm，否则结果无法收敛；
5. **队列容量**：`MININVM_QUEUE_SIZE` 应 ≥ `MININVM_BLOCK_COUNT`，以支持一次 `ReadAll`/`WriteAll` 全量入队；
6. **内存占用**：队列每个条目含 `Data[MININVM_MAX_BLOCK_LENGTH]`，队列容量乘以最大块长决定 RAM 开销，移植时需评估。

## 9. 验证要求

- 单块读写、异步结果与状态映射；
- ReadAll / WriteAll 与各 RAM buffer 数据一致性（覆盖全部 Block）；
- 队列、重复请求、取消；
- 默认值恢复与非法参数处理；
- 簇迁移 / 翻页与簇头（CRC-8 + Magic + Generation）校验；
- 长度非对齐数据由 MiniFee 处理，MiniNvm 不参与。
