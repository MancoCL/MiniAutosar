# MiniFee 软件详细设计

| 项目 | 内容 |
| --- | --- |
| 模块名称 | MiniFee |
| 所属层级 | BSW / SystemServices / BootServices |
| 上层模块 | MiniNvm / 直接调用方（vss、SecureBoot 等） |
| 下层模块 | MiniFlsIf |
| 设计版本 | V1.1 |
| 作者 | Manco |
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

### 2.1 配置项与取值依据（`MiniFee_Cfg.h`）

下表中的配置项均由移植者按目标工程决定，MiniFee 代码不写死任何具体值：

| 配置项 | 性质 | 取值依据 / 约束 |
| --- | --- | --- |
| `MINIFEE_FLS_BASE` | 平台相关 | 必须等于底层 Fls 驱动的地址基址（MiniFee 只向 MiniFlsIf 传“绝对地址 − 基址”的偏移），由目标 Flash 映射决定 |
| `MINIFEE_VIRTUALPAGE_SIZE` | 平台相关 | 必须等于底层 Fls 的最小写入单元；取值过大会浪费簇空间，过小会违反 Flash 编程约束 |
| `MINIFEE_MAX_BLOCK_DATA_SIZE` | 内存相关 | 必须 ≥ `MiniFee_BlockConfig[]` 中所有 `Length` 的最大值；直接决定内部读/写缓冲大小（RAM），详见 §5.7 |
| `MINIFEE_BLOCK_COUNT` | 由配置推导 | 等于枚举哨兵 `MINIFEE_BLOCK_MAX`，随块表条目数自动变化，**不得手改** |
| `MiniFee_BlockConfig[]` | 项目数据 | 每项 `{BlockNumber, Length}`；`BlockNumber` 须等于数组下标；`Length` 为各业务块实际数据长度（`0` 表示空块） |
| `MINIFEE_CLUSTER_COUNT` | 由配置推导 | 必须等于 `MiniFee_ClusterConfig[]` 条目数，且 **≥ 2** 才支持轮换迁移 |
| `MiniFee_ClusterConfig[]` | 项目数据 | 每项 `{StartAddress, Length}`：在目标 Flash 中划分互不重叠的簇，地址须按擦除单元对齐、长度须为其整数倍 |

> 簇容量须容纳“全部块各一条最新记录”，规划见 §5.5；块表/簇表变更时须同步 MiniNvm 块描述符（见 MiniNvm 详设）。

## 3. 对外接口

| 接口 | 原型 | 说明 |
| --- | --- | --- |
| `MiniFee_Init` | `void MiniFee_Init(void)` | 初始化 MiniFlsIf 并复位上下文 |
| `MiniFee_Read` | `uint8 MiniFee_Read(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf)` | 发起指定 Block 的异步读 |
| `MiniFee_Write` | `uint8 MiniFee_Write(MiniFee_BlockIdType blockNumber, uint32 size, uint8* buf)` | 发起指定 Block 的异步写 |
| `MiniFee_ReadAll` | `uint8 MiniFee_ReadAll(MiniFee_ReadAllCallbackType callback)` | 发起异步一次性读全部 Block：单遍扫描活动簇、逐条回调交付 |
| `MiniFee_WriteAll` | `uint8 MiniFee_WriteAll(MiniFee_WriteAllDataCallbackType callback)` | 发起异步一次性写全部 Block：单遍选簇后连续追加待写记录 |
| `MiniFee_Cancel` | `uint8 MiniFee_Cancel(void)` | 取消当前异步任务 |
| `MiniFee_MainFunction` | `void MiniFee_MainFunction(void)` | 推进状态机（每次只推进有限状态，不同步等待 Flash） |
| `MiniFee_GetStatus` | `MiniFee_StatusType MiniFee_GetStatus(void)` | 查询任务状态 |

**一次性读回调 `MiniFee_ReadAllCallbackType`：** `void (*)(uint8 blockNumber, const uint8* data, uint32 length)`。`MiniFee_ReadAll` 单遍扫描活动簇，每读到一条有效记录即同步回调一次；`blockNumber`（0 起）为该记录块号，`data`/`length` 为记录数据与逻辑长度。同一块追加多条记录时后读到的（更新的）覆盖先读到的。

**一次性写取数回调 `MiniFee_WriteAllDataCallbackType`：** `const uint8* (*)(uint8 blockNumber)`。返回该块（0 起）待写数据指针，返回 `NULL` 表示跳过；数据长度取 `MiniFee_BlockConfig[].Length`。`MiniFee_WriteAll` 先遍历回调**预统计总占用**、据此单遍选簇（空间不足先迁移），再按块号顺序连续追加全部待写记录。回调会被“预统计”与“实际写入”各调用一次，须稳定且数据在写作业期间有效。

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

### 4.1 Cluster 头（`MINIFEE_CLUSTER_HEADER_SIZE` 字节，大端）

| 字段 | 偏移 | 长度 | 说明 |
| --- | --- | --- | --- |
| Magic | 0 | 4 | `MINIFEE_CLUSTER_MAGIC`，识别已初始化区域 |
| Generation | 4 | 3 | 激活代次（大端），每次激活自增；活动簇凭最大 Generation 判定 |
| CheckSum | 7 | 1 | CRC-8（多项式 0x07），输入为前 7 字节 |

**Generation 自增规则**：活动簇 Generation 在目录构建（`FIND_SUB_DECIDE`，随簇头解析）时一并缓存到 `activeClusterGen`；写簇头时直接取该缓存值 +1，**不再回读一次源簇头**（每次写簇头由“1 读 + 1 写”降为“1 写”）。不依赖外部参数；若无有效活动簇（首次激活/空白）则从 0 开始（写入 1）。

**写缓冲约束**：底层 `Fls_Write` 为异步接口，仅在调用时保存源缓冲指针，实际数据在后续 `Fls_MainFunction` 中分多次读取。因此簇头写入的源缓冲必须是**持久缓冲**（上下文成员 `writeHdrBuf`），禁止使用栈上临时数组，否则簇头在写入完成前会被后续函数调用复用的栈空间破坏。

### 4.2 记录（`MINIFEE_BLOCK_HEADER_SIZE` 字节块头 + 数据）

| 字段 | 偏移 | 长度 | 说明 |
| --- | --- | --- | --- |
| BlockNumber | 0 | 3 | 逻辑 Block 号（大端） |
| Length | 3 | 4 | 逻辑数据长度（非物理占用，大端） |
| CheckSum | 7 | 1 | CRC-8（多项式 0x07），输入为 `BlockNumber(3) + Length(4)` 共 7 字节 |


- 数据区物理占用 = `MINIFEE_ALIGN_LEN(Length)`，即按 `MINIFEE_VIRTUALPAGE_SIZE` 向上对齐；真实数据在前 `Length` 字节，其余以 0 填充；
- 记录物理步长 = `MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(Length))`；
- 块头 + 数据在**同一次 `Fls_Write`** 中写入，块内不存在事后单独编程的标志页（底层数据 Flash 同一区域两次擦除之间仅允许一次编程）。字段偏移由固定序列化格式决定，不可配置。

### 4.3 CRC-8

多项式 `0x07`，初始值 `0x00`，不反相，逐字节计算。簇头校验输入为前 7 字节；块头校验输入为 `BlockNumber(3, 大端) + Length(4, 大端)`。

## 5. 状态机设计

### 5.1 主状态 `MiniFee_StateType`

| 状态 | 说明 |
| --- | --- |
| `MINIFEE_STATE_IDLE` | 空闲 |
| `MINIFEE_STATE_FLS_WAIT` | 通用 MiniFlsIf 异步等待 |
| `MINIFEE_STATE_READ_BY_DIR` | 单块读：按块目录读目标块最新数据 |
| `MINIFEE_STATE_READ_VERIFY_COPY` | 读流程：拷贝逻辑数据 |
| `MINIFEE_STATE_READ_DONE` | 读流程：收尾 |
| `MINIFEE_STATE_READALL_FILL` | 一次性读：按目录逐块读最新数据并回调 |
| `MINIFEE_STATE_WRITE_MAIN_LOOP_PROC` | 写流程：组装块头+数据并单次追加写入 |
| `MINIFEE_STATE_WRITE_DONE` | 写流程：收尾 |
| `MINIFEE_STATE_WRITEALL_APPEND` | 一次性写：选簇后连续追加待写记录 |
| `MINIFEE_STATE_FIND_PROC` | FindAddr / 簇选择与块目录构建子流程 |
| `MINIFEE_STATE_ERROR` | 错误收尾 |

### 5.1.1 块目录（RAM 缓存）

为把“查找某块最新记录”从“每次扫描记录头”降为常数次 Fls 读，MiniFee 在 RAM 维护块目录：

| 成员 | 说明 |
| --- | --- |
| `dirOffset[MINIFEE_BLOCK_COUNT]` | 每块最新记录块头在活动簇内的偏移；`0xFFFFFFFF`（无记录哨兵，固定格式常量）表示该块无记录 |
| `dirValid` | 目录是否对当前活动簇有效 |
| `activeClusterStart/Length`、`headerEnd`、`activeClusterGen` | 与目录同源的活动簇、写入游标及其 Generation 缓存，跨作业保留 |

- **构建**：`FIND_SUB_DECIDE` 后单遍扫描活动簇块头（`FIND_SUB_METRICS_*`），逐条 `dirOffset[BlockNumber] = scanOff`（后覆盖前，取最新）；`METRICS_DONE` 或迁移完成后置 `dirValid=1`；
- **复用**：目录有效时，单块读跳过 FindAddr/扫描，直接 `Fls_Read(activeClusterStart + dirOffset[block] + MINIFEE_BLOCK_HEADER_SIZE, ...)`（0~1 次读）；写入跳过扫描，直接追加（0 读）；**即便因空间不足进入 FindAddr，`DECIDE` 也复用现有目录、不再重扫活动簇**（见 §5.6）；
- **失效/更新**：追加写更新 `dirOffset[block]`；迁移重建目录；`MiniFee_Cancel` 置 `dirValid=0`；`MiniFee_Init` 清空。冷启动首次操作代价为一次目录构建扫描，之后均为常数次读。
- **有效性范围与写保护**：`dirValid=1` 只表示“目录与 `activeClusterIndex` 对应、可安全复用”。空白 Flash 的**读**请求会建立一个**空目录**（`dirValid=1` 但 `activeClusterGen=0xFFFFFFFF`，此时簇头尚未写入）。因此**写请求的快路径还须满足 `activeClusterGen != 0xFFFFFFFF`**，否则会把记录追加到没有簇头的簇上、下次上电后读不回来；不满足时改走 FindAddr，由首次激活（擦簇 + 写簇头）建立有效活动簇。

### 5.2 FindAddr 子状态 `MiniFee_FindSubState`

FindAddr 负责“选活动簇 + 空间/健康指标扫描 + 必要时迁移”，是一个可被 `Fls_Wait` 反复打断的异步子状态机：

| 子状态 | 说明 |
| --- | --- |
| `FIND_SUB_START` | 逐簇读取簇头 |
| `FIND_SUB_READ_CLUSTER_HDR_DONE` | 解析簇头，取 CRC 有效且 Magic 匹配者中 Generation 最大者为活动簇 |
| `FIND_SUB_DECIDE` | 决策：无活动簇时，写请求→首次激活簇 0，读请求→建空目录（返回全零）；**目录已对所选活动簇有效时直接复用**（写请求且空间不足→直接进迁移准备，不再重扫活动簇） |
| `FIND_SUB_FRESH_ERASE` | 首次激活：擦除簇 0 |
| `FIND_SUB_FRESH_ACTIVATE` | 首次激活：写簇 0 簇头 |
| `FIND_SUB_FRESH_ACTIVATE_DONE` | 首次激活收尾：设置活动簇游标、清空块目录并校验空间 |
| `FIND_SUB_METRICS_READ` | 扫描活动簇：逐条读块头 |
| `FIND_SUB_METRICS_EVAL` | 评估块头：合法则登记入块目录并推进 `headerEnd`，非法则标记损坏 |
| `FIND_SUB_METRICS_DONE` | 汇总：写请求且（损坏或空间不足）→ 迁移；否则块目录有效 |
| `FIND_SUB_MIGRATE_PREPARE` | 迁移准备：目标簇=(活动簇+1)%簇数，块游标清 0 |
| `FIND_SUB_MIGRATE_ERASE_DST` | 擦除目标簇 |
| `FIND_SUB_MIGRATE_READ` | 取下一个目标簇待写块：本次作业要覆盖的块**直接组装新数据（0 读）**，否则读源记录数据 |
| `FIND_SUB_MIGRATE_WRITE` | 写目标簇新记录，并把目录改指目标簇位置 |
| `FIND_SUB_MIGRATE_ACTIVATE` | 写目标簇簇头（Generation = `activeClusterGen` + 1） |
| `FIND_SUB_MIGRATE_SWITCH` | 切换活动簇到目标簇（老簇保留），块目录置为有效；本次写入已随迁移完成，直接收尾 |
| `FIND_SUB_WRITE_HDR_WRITE` | 写簇头子流程：用缓存的 Generation 写目标簇头 |
| `FIND_SUB_DONE` | 子流程结束，返回主状态 |

### 5.3 读流程

1. `MiniFee_Read` 校验参数，置 `BUSY`；**目录有效**则直接进入 `READ_BY_DIR`，否则先 FindAddr（`FIND_TYPE_READ`，构建块目录）；
2. `READ_BY_DIR`：查 `dirOffset[block]`；为 `0xFFFFFFFF`（无记录）则向用户缓冲填全零并 `READ_DONE`；否则一次 `Fls_Read` 读该记录数据到 `blockDataBuf`；
3. `READ_VERIFY_COPY`：只复制 `logicalBlockSize` 到上层缓冲；
4. `READ_DONE` → `FinalizeOk`。

**一次性读（`MiniFee_ReadAll`）**：目录有效直接进 `READALL_FILL`，否则先 FindAddr 构建目录；随后按块号顺序、对每块**只读其最新记录**并经回调交付（不再读被覆盖的旧记录）；无记录块不回调（上层保持默认全零）。

### 5.4 写流程

写入不与旧记录比对（去重交由上层：MiniNvm 仅在数据实际变化时置脏、且只写脏块）。

1. `MiniFee_Write` 校验参数，置 `BUSY`；**目录有效、活动簇有效（`activeClusterGen != 0xFFFFFFFF`）且剩余空间足够**则直接进入 `WRITE_MAIN_LOOP_PROC`，否则先 FindAddr（`FIND_TYPE_WRITE`，选簇/迁移/构建目录）；
2. `WRITE_MAIN_LOOP_PROC`：组装块头（`Length = logicalBlockSize`、CRC-8）与对齐数据，**单次**追加写入 `headerEnd` 处；更新 `headerEnd/dirOffset[block]`；
3. `WRITE_DONE` → `FinalizeOk`。

> 若本次写入触发迁移，则被覆盖的块已在迁移阶段（§5.5）按“新数据”写过一次，作业的写入阶段被跳过，直接收尾；且若目录此前已有效，FindAddr 会复用目录、不再重扫活动簇（§5.6）。

**一次性写（`MiniFee_WriteAll`）**：先遍历取数回调预统计总占用，再进入 `WRITEALL_APPEND`（目录有效且空间足够则跳过选簇扫描，否则先 FindAddr 选簇/一次性迁移），按块号顺序连续追加所有待写记录（每条更新 `headerEnd/dirOffset`）。

### 5.5 迁移与磨损均衡

- 前置条件（容量规划）：单簇须能容纳“全部块各一条最新记录”，即 `簇 Length ≥ MINIFEE_CLUSTER_HEADER_SIZE + Σ MINIFEE_BLOCK_TOTAL_OF(MINIFEE_ALIGN_LEN(各块 Length))`，建议预留 ≥ 2 倍；不满足则迁移会因目标簇不足而失败；
- 触发条件：活动簇存在损坏记录，或剩余空间不足以容纳本次写入；
- Fls 开销（目录有效时不免除选簇读与擦除）：选簇读 `MINIFEE_CLUSTER_COUNT` 次 + 1 次擦除 + 非本次作业块各 1 读 1 写 + 新簇头 1 写；**目录已有效时不再重扫活动簇**（§5.6），本次作业要覆盖的块不读旧数据；
- 目标簇 = `(activeClusterIndex + 1) % MINIFEE_CLUSTER_COUNT`；
- **仅在写入前擦除目标簇**；源簇内容保留，断电/异常时可回退，源簇待下次作为迁移目标时再擦除；
- **单遍迁移（迁移即写入）**：块号 0..N-1 遍历，对每个块按优先级处理——
  1. **本次作业要覆盖的块**（单块写 = 目标块；一次性写 = 取数回调返回非 `NULL`）→ **直接用新数据组装记录写入目标簇（0 次读）**；
  2. 其余在源簇有记录的块 → 从源簇该偏移读出旧数据、追加写到目标簇 `migrateDstHdrEnd` 处；
  3. 两者皆无 → 跳过，不占目标簇空间。

  每条写完即把 `dirOffset[b]` 改指目标簇位置（块头无链，无需去重位图）。同一块旧数据只搬运一次、新数据只写一次，**被本次作业覆盖的块全程只写 1 次**。每条写入前做目标簇容量防御性校验（正常配置下全部块记录必能放入单簇），越界即失败退出；
- 迁移完成后写目标簇头（Generation = `activeClusterGen` + 1），切换活动簇、`headerEnd` 置为目标簇已用末尾、`activeClusterGen` 更新为新 Generation；源簇头保持原状，不二次改写；
- 因被覆盖块已在迁移阶段写入，**迁移完成后本次作业的写入阶段（`WRITE_MAIN_LOOP_PROC` / `WRITEALL_APPEND`）被跳过**，直接收尾。

### 5.6 目录构建扫描

- **复用优先（免重扫）**：进入扫描前先判断“目录是否已对所选活动簇有效”——`dirValid != 0` 且 `bestGenIdx == activeClusterIndex` 且 `bestGen == activeClusterGen`。成立则**整段扫描跳过**：写请求空间不足时直接跳 `FIND_SUB_MIGRATE_PREPARE`，否则直接 `FIND_SUB_DONE`；省去 `活动簇记录条数 + 1` 次块头读（迁移路径下即为纯收益）；
- 从 `MINIFEE_CLUSTER_HEADER_SIZE` 开始逐条读块头；
- 全 `MINIFEE_ERASED_VALUE` 表示到达簇尾空白，结束；
- 块头 CRC 失败或长度非法视为损坏，结束并标记 `scanCorrupt`；
- 合法记录：`dirOffset[BlockNumber] = scanOff`（后覆盖前，取最新），并推进 `headerEnd`。
- 扫描同时把活动簇的 Generation 缓存到 `activeClusterGen`（取各簇头中 CRC 有效且最大者），供后续写簇头直接取用，无需回读。

### 5.7 作业数据缓冲（RAM）

上下文里两块“作业数据”缓冲合成同一 `union`（`jobBuf`），省一份最大块长缓冲：

| 成员 | 大小 | 使用方 |
| --- | --- | --- |
| `blockDataBuf[MINIFEE_MAX_BLOCK_DATA_SIZE]` | `MINIFEE_MAX_BLOCK_DATA_SIZE` | **读作业**：`READ_BY_DIR`/`READALL_FILL` 读入当前块数据，`READ_VERIFY_COPY` 拷逻辑长度到用户缓冲 |
| `writeBuf[MINIFEE_BLOCK_HEADER_SIZE + MINIFEE_MAX_BLOCK_DATA_SIZE]` | 比上一项多 `MINIFEE_BLOCK_HEADER_SIZE` | **写作业**：单块写、`WRITEALL_APPEND`、迁移 `MIGRATE_READ/WRITE`（块头+数据合并写缓冲） |

- 合并前提：MiniFee 单上下文、`Busy` 时拒绝新请求，**读作业与写作业不并发**；且读作业只用 `blockDataBuf`、写作业（含迁移）只用 `writeBuf`（迁移只在写请求下发生），互斥使用，共用同一块 RAM 安全；
- 缓冲峰值恒为“一条最大块记录”（`MINIFEE_BLOCK_HEADER_SIZE + MINIFEE_MAX_BLOCK_DATA_SIZE`），与块数、单次写入总量无关；
- 其余缓冲独立保留：`clusterHdrBuf[MINIFEE_CLUSTER_HEADER_SIZE]`、`scanHdrBuf[MINIFEE_BLOCK_HEADER_SIZE]`、`writeHdrBuf[MINIFEE_CLUSTER_HEADER_SIZE]`（`writeHdrBuf` 须持久，见 §7.1）。

## 6. 错误处理与约束

- Busy 时拒绝新请求；非法 Block、长度不匹配或空指针返回 `E_NOT_OK`；
- 底层 Pending 时保持上下文，不重复发起请求；底层失败、取消或迁移失败返回失败状态；读流程遇到损坏/缺失记录不报错，按未找到处理（返回全零）；
- 不提供字节偏移访问、Dataset、冗余 Block 或上层 RAM 管理；
- 所有底层访问经 MiniFlsIf，不直接依赖具体 Flash 驱动；
- 任何区域仅一次编程：不写 Valid/Invalid 标志，不进行 1→0 二次编程。

## 7. 关键注意事项

1. **持久写缓冲**：`writeBuf`（`jobBuf` 联合体成员）、`writeHdrBuf` 是上下文成员，因 `Fls_Write` 异步持有源指针，禁止改用栈上临时数组；读/写作业缓冲合并在 `jobBuf` 联合体（§5.7），禁止跨类型混用同一作业；
2. **逻辑长度 vs 物理长度**：`logicalBlockSize` 为上层数据长度，`blockSize` 为按 `MINIFEE_VIRTUALPAGE_SIZE` 对齐后的物理长度；上层只会看到逻辑长度；
3. **记录布局**： `BlockNumber(3)+Length(4)+CRC-8(1)`；“某块最新记录”由 RAM 块目录维护；
4. **簇数量须 ≥ 2**：迁移采用双簇轮换；`MINIFEE_CLUSTER_COUNT` 必须与 `MiniFee_ClusterConfig[]` 条目数一致；
5. **地址对齐**：Cluster `StartAddress` 须与 Fls 擦除单元对齐，`Length` 须为擦除单元整数倍；
6. **缓冲区容量**：`MINIFEE_MAX_BLOCK_DATA_SIZE` 须 ≥ 所有块 `Length` 的最大值；
7. **配置一致性**：`MiniFee_BlockConfig[i].BlockNumber` 必须等于 `i`；`MiniFee_BlockIdType` 哨兵 `MINIFEE_BLOCK_MAX` 自动决定 `MINIFEE_BLOCK_COUNT`；
8. **块目录（RAM）**：上下文维护 `dirOffset[MINIFEE_BLOCK_COUNT]`（每块 4 字节）与 `dirValid`，开销随块数线性变化；冷启动首次读/写构建一次目录，之后单块读仅 1 次数据读、单块写 0 读；追加写更新目录、迁移重建、`Init`/`Cancel` 使其失效。
9. **Generation 缓存**：活动簇 Generation 随目录构建缓存于 `activeClusterGen`，写簇头不再回读源簇头（每次写簇头由 1 读 + 1 写降为 1 写）；无有效活动簇（首次激活/空白）时置无记录哨兵 `0xFFFFFFFF`，自增后写入首个有效值（1）。
10. **迁移即写入**：迁移阶段对“本次作业要覆盖的块”直接写新数据，并在迁移完成后跳过本次作业的写入阶段，使这些块从“搬运 1 读 1 写 + 更新 1 写”降为“只写 1 次”；代价是迁移需感知本次作业的取数来源（单块写取 `buf`，一次性写取回调，见 §5.5）。

## 8. 验证要求

- 单 Block 异步读写与状态映射；
- 块目录：冷启动首次操作构建一次目录，之后单块读/写为常数次 Fls 读；追加写/迁移后目录正确更新；
- Flash 空白或该 Block 无记录时，读取返回全零且成功（不返回失败）；
- 写入不做内容去重（脏块过滤由 MiniNvm 完成），写入后读回等于写入数据；
- 记录校验失败处理；
- 簇迁移/翻页及断电回退（老簇保留）；
- 迁移即写入：被本次作业覆盖的块只写一次（迁移阶段写新数据，迁移后不再补写）；未被覆盖的块旧数据完整搬运、结果一致；
- Generation 缓存：写簇头不再回读源簇头，迁移后活动簇 Generation 正确递增；
- 多次整块重写触发迁移后数据保全与 Generation 递增；
- Flash 写入粒度与物理对齐正确处理。