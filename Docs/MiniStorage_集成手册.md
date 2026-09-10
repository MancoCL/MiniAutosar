# MiniFee / MiniNvm / MiniFlsIf 集成手册

| 项目 | 内容 |
| --- | --- |
| 文档名称 | MiniFee / MiniNvm / MiniFlsIf 集成手册 |
| 适用模块 | MiniFee、MiniNvm、MiniFlsIf |
| 设计版本 | V1.0 |
| 作者 | CaoLiang |
| 目标 | 使三个模块能够快速移植到其他工程 / 平台，并说明全部可配置项的功能与注意事项 |

---

## 1. 模块总览

三个模块构成一套精简的非易失存储栈，自下而上为：

```text
应用 / 上层业务
    |  MiniNvm API（逻辑块、队列、结果）
    v
MiniNvm
    |  MiniFee API（逻辑 Block、记录、CRC、迁移、磨损均衡）
    v
MiniFee
    |  MiniFlsIf API（异步 Flash 适配）
    v
MiniFlsIf
    |  平台 Flash 驱动（Fls MCAL 等）
    v
Flash
```

| 模块 | 职责 | 是否与平台绑定 |
| --- | --- | --- |
| MiniFlsIf | 转发底层 Flash 异步接口、状态与周期推进 | **是**（唯一需要改动的移植点） |
| MiniFee | 逻辑 Block 存储、CRC 校验、追加写入、簇迁移、磨损均衡 | 否（仅依赖配置） |
| MiniNvm | RAM Block 管理、单块/多块异步请求队列与结果 | 否（仅依赖配置） |

**调用关系：** MiniNvm 调用 MiniFee，MiniFee 调用 MiniFlsIf；反向无依赖。

**异步模型：** 三者均为异步状态机。`Read/Write` 类接口返回 `E_OK` 只表示请求被接受，必须周期调用各自 `MainFunction` 推进，最终结果经状态查询接口获取。

---

## 2. 文件清单

| 模块 | 文件 | 说明 |
| --- | --- | --- |
| MiniFlsIf | `MiniFlsIf.h` / `MiniFlsIf.c` | 平台适配接口与实现 |
| MiniFee | `MiniFee.h` / `MiniFee.c` | 存储核心（静态代码） |
| MiniFee | `MiniFee_Cfg.h` / `MiniFee_Cfg.c` | 可变配置（地址基址、块表、簇表） |
| MiniNvm | `MiniNvm.h` / `MiniNvm.c` | 逻辑块与请求队列 |
| MiniNvm | `MiniNvm_Cfg.h` / `MiniNvm_Cfg.c` | 块描述符与 RAM buffer |
| MiniNvm | `MiniNvm_Test.h` / `MiniNvm_Test.c` | 开发阶段自检（**破坏性，量产须移除调用**） |

---

## 3. 工程集成

### 3.1 加入源码与包含路径

将上述 `.c` / `.h` 加入目标工程，并把各模块目录加入包含路径。依赖的公共头文件：

| 头文件 | 提供内容 |
| --- | --- |
| `Common.h` | `uint8` / `uint32`、`E_OK` / `E_NOT_OK`、`NULL_PTR`、`CommF_DataSet` / `CommF_DataCopy` |
| `MemIf_Types.h` | `MemIf_StatusType`、`MemIf_JobResultType` |
| `Std_Types.h` | `Std_ReturnType` 等（由 `Common.h` 间接包含） |

### 3.2 初始化

```c
MiniNvm_Init();     /* 内部会调用 MiniFee_Init → MiniFlsIf_Init */
```

若只使用 MiniFee 而不使用 MiniNvm，则调用 `MiniFee_Init()`。

### 3.3 周期调度

必须按固定顺序独立调度三个 MainFunction，**不可省略任一层的推进**：

```c
MiniFlsIf_MainFunction();   /* 推进底层 Flash */
MiniFee_MainFunction();     /* 推进存储状态机 */
MiniNvm_MainFunction();     /* 推进请求队列 */
```

推荐放在 1 ms 或更快的周期任务中，并在循环中喂看门狗。MiniNvm **不会**代调用 MiniFee / MiniFlsIf 的 MainFunction。

### 3.4 典型使用

```c
uint8 buf[MINIFEE_MAX_BLOCK_DATA_SIZE];

/* 单块写 */
if (MiniNvm_WriteBlock(1u, buf) == E_OK)
{
    MiniNvm_RequestResultType r;
    do {
        MiniFlsIf_MainFunction();
        MiniFee_MainFunction();
        MiniNvm_MainFunction();
        (void)MiniNvm_GetErrorStatus(1u, &r);
    } while (r == MININVM_REQ_PENDING);
    /* r 为 MININVM_REQ_OK / NOT_OK / RESTORED_FROM_ROM */
}

/* 单块读 */
if (MiniNvm_ReadBlock(1u, buf) == E_OK) { /* 同上轮询 */ }

/* 全量读写 */
MiniNvm_WriteAll();
MiniNvm_ReadAll();
```

> 注意：`MiniNvm_WriteBlock` 在**入队时**即复制源数据，入队返回后可立即复用源缓冲；`MiniNvm_ReadBlock` 的数据在作业完成后才写入目标缓冲。

---

## 4. 可配置项详解

### 4.1 MiniFlsIf 平台映射（`MiniFlsIf.c`）

| 配置/映射 | 当前值 | 功能与注意事项 |
| --- | --- | --- |
| `MiniFlsIf_Init` | `Fls_Init(FlsConfigSet)` | 初始化底层 Flash。目标平台若需时钟/控制器使能，请在此补充 |
| `MiniFlsIf_Read/Write/Erase` | 转发 `Fls_Read/Write/Erase` | **必须保持异步**；若目标驱动为同步，需在适配层内实现异步状态机，否则会破坏 MiniFee 非阻塞假设 |
| `MiniFlsIf_GetStatus/GetJobResult` | 转发 `Fls_GetStatus/GetJobResult` | 若目标驱动无 `MemIf` 风格状态，需在适配层维护状态机并映射为 `MemIf_*` 枚举 |
| `MiniFlsIf_MainFunction` | `Fls_MainFunction` | 周期推进底层任务 |
| `MiniFlsIf_DeInit` | 空实现 | 需要关闭 Flash 控制器/时钟时补充 |
| `FlsConfigSet` | 工程 Fls 配置 | 地址范围、擦除单元、写单元必须与 MiniFee 配置匹配 |

### 4.2 `MiniFee_Cfg.h` 配置宏

| 宏 | 当前值 | 功能 | 约束与注意事项 |
| --- | --- | --- | --- |
| `MINIFEE_FLS_BASE` | `0xFF200000UL` | Flash 地址基址。MiniFee 内部偏移 = 绝对地址 − 该基址 | 必须等于底层 Fls 驱动的地址基址，否则所有读写擦都会指向错误位置 |
| `MINIFEE_VIRTUALPAGE_SIZE` | `4u` | Flash 最小写入单元，用于数据物理对齐 | 必须等于底层 Fls 的写粒度；记录物理步长 = `8 + align(Length, 此值)`；过大会浪费空间，过小会违反 Flash 编程约束 |
| `MINIFEE_MAX_BLOCK_DATA_SIZE` | `880u` | 单块数据缓冲上限，内部 `blockDataBuf` / `writeBuf` 按此定容 | 必须 ≥ 所有块 `Length` 最大值；直接决定 RAM 占用，改小会截断大数据块 |

### 4.3 块配置（`MiniFee_BlockIdType` + `MiniFee_BlockConfig[]`）

| 项 | 功能 | 约束与注意事项 |
| --- | --- | --- |
| `MiniFee_BlockIdType` 枚举 | 定义逻辑块号 | `BlockNumber` 必须等于数组下标（0..N−1）；哨兵 `MINIFEE_BLOCK_MAX` 必须置于最后，`MINIFEE_BLOCK_COUNT` 由其自动推导 |
| `MiniFee_BlockConfig[].BlockNumber` | 块号 | 必须等于数组下标，`MiniFee_Read/Write` 会校验 |
| `MiniFee_BlockConfig[].Length` | 该块逻辑数据长度 | 调用 `MiniFee_Read/Write` 时 `size` 必须严格等于此值；`0` 表示空块（不占存储） |
| `MINIFEE_BLOCK_COUNT` | 块总数 | 由枚举哨兵推导，同时决定迁移去重位图大小（每块 1 bit） |

**新增/删除块步骤：**

1. 在 `MINIFEE_BLOCK_MAX` 之前增加/删除枚举项；
2. 在 `MiniFee_BlockConfig[]` 中对应位置增加/删除条目，保证 `BlockNumber == 下标`；
3. 同步修改 `MiniNvm_BlockDescriptor[]` 与对应 RAM buffer（见 4.5）；
4. 若新块长度超过当前 `MINIFEE_MAX_BLOCK_DATA_SIZE`，同步增大该宏。

### 4.4 簇配置（`MiniFee_ClusterConfig[]` + `MINIFEE_CLUSTER_COUNT`）

| 项 | 当前值 | 功能 | 约束与注意事项 |
| --- | --- | --- | --- |
| `StartAddress` | `0xFF200000` / `0xFF202000` | 簇起始绝对地址 | 必须与 Flash 擦除单元对齐 |
| `Length` | `0x2000` / `0x2000` | 簇长度 | 必须为擦除单元整数倍；两簇**不可重叠**，且不能覆盖其他数据区 |
| `MINIFEE_CLUSTER_COUNT` | `2u` | 簇数量 | **必须 ≥ 2** 才能支持迁移轮换；必须与 `MiniFee_ClusterConfig[]` 条目数一致 |

**容量规划：** 每个簇需容纳“全部块各一条最新记录”。粗略需求：

```text
单簇最小长度 ≥ 簇头(8) + Σ( 8 + align(各块Length) ) × 安全系数
```

空间不足时 MiniFee 会触发迁移，但迁移后仍不足会返回失败。建议预留 ≥ 2 倍。

### 4.5 MiniNvm 配置（`MiniNvm_Cfg.h` / `MiniNvm_Cfg.c`）

| 配置 | 当前值 | 功能 | 约束与注意事项 |
| --- | --- | --- | --- |
| `MININVM_BLOCK_COUNT` | `MINIFEE_BLOCK_MAX` | 逻辑块数量 | 跟随 MiniFee，不要单独修改 |
| `MININVM_MAX_BLOCK_LENGTH` | `MINIFEE_MAX_BLOCK_DATA_SIZE` | 单块最大长度 | 跟随 MiniFee |
| `MININVM_QUEUE_SIZE` | `MININVM_BLOCK_COUNT` | 请求队列容量 | 需 ≥ 块数才能一次 `ReadAll`/`WriteAll` 全量入队；**每个队列条目内嵌最大块缓冲，RAM 开销 = 队列长度 × (MAX_BLOCK_LENGTH + 元数据)**，详见第 6 节 |
| `MiniNvm_RamBlock_N[]` | 各块独立 RAM buffer | 上层数据实际存放处 | 长度必须等于对应块 `Length`；`RamBlockAddress` 不可为空 |
| `MiniNvm_BlockDescriptor[].BlockId` | 1..N | MiniNvm 块号（**从 1 开始**） | 必须等于下标 + 1 |
| `MiniNvm_BlockDescriptor[].MiniFeeBlockId` | 0..N−1 | 对应 MiniFee 块号（**从 0 开始**） | 必须等于下标，与 MiniFee 块配置一一对应 |
| `MiniNvm_BlockDescriptor[].Length` | 各块长度 | 逻辑长度 | 必须等于对应 `MiniFee_BlockConfig[].Length` |
| `MiniNvm_BlockDescriptor[].RomBlockAddress` | `NULL_PTR` | 可选 ROM 默认值地址 | 读取失败且无回调时，无法恢复则结果 `NOT_OK` |
| `MiniNvm_BlockDescriptor[].InitBlockCallback` | `NULL_PTR` | 可选默认值初始化回调 | 原型 `Std_ReturnType (*)(uint8 blockId, uint8* ramPtr)` |

> `MiniNvm_Init` 会逐项校验：`BlockId == 下标+1`、`MiniFeeBlockId == 下标`、`Length == MiniFee_BlockConfig[下标].Length`、`Length ≤ MAX_BLOCK_LENGTH`、`RamBlockAddress != NULL`。任一不满足则模块保持 `UNINIT`，所有请求返回 `E_NOT_OK`。

---

## 5. 移植到新工程 / 平台步骤

1. **复制源码**：将三个模块目录复制到目标工程；
2. **适配平台层**：只修改 `MiniFlsIf.c`，把接口映射到目标 Flash 驱动（见 4.1）；
3. **确定地址布局**：在目标 Flash 中划分两个（或更多）互不重叠、按擦除单元对齐的簇，填写 `MiniFee_ClusterConfig[]`；
4. **确定地址基址**：设置 `MINIFEE_FLS_BASE` 与 Fls 驱动基址一致；
5. **确定写粒度**：设置 `MINIFEE_VIRTUALPAGE_SIZE` 等于 Fls 写单元；
6. **定义块表**：按业务定义 `MiniFee_BlockIdType` / `MiniFee_BlockConfig[]`，同步 `MiniNvm` 描述符与 RAM buffer；
7. **设置上限**：`MINIFEE_MAX_BLOCK_DATA_SIZE` ≥ 最大块长；
8. **接入调度**：在周期任务中按 `MiniFlsIf → MiniFee → MiniNvm` 顺序调用 MainFunction；
9. **初始化**：调用 `MiniNvm_Init()`（或 `MiniFee_Init()`）；
10. **移除自检**：删除对 `MiniNvm_Test()` 的调用（若原工程引入了测试代码）；
11. **验证**：烧录后执行读写，确认 `GetStatus/GetErrorStatus` 返回预期结果，并检查 map 中的 RAM 占用。

### 5.1 移植检查清单

- [ ] `MINIFEE_FLS_BASE` 与 Fls 驱动基址一致
- [ ] `MINIFEE_VIRTUALPAGE_SIZE` 与 Fls 写粒度一致
- [ ] 簇地址按擦除单元对齐、长度为其整数倍、互不重叠
- [ ] `MINIFEE_CLUSTER_COUNT` 与簇表条目数一致且 ≥ 2
- [ ] `MiniFee_BlockConfig[].BlockNumber` 等于数组下标
- [ ] `MINIFEE_MAX_BLOCK_DATA_SIZE` ≥ 最大块长
- [ ] MiniNvm 描述符数量/顺序/长度与 MiniFee 块表一一对应
- [ ] `MiniNvm_RamBlock_N` 长度等于对应块长
- [ ] 周期任务按正确顺序调用三个 MainFunction
- [ ] 已移除 `MiniNvm_Test()` 等破坏性自检调用

---

## 6. 内存与性能评估

| 模块 | 主要 RAM 开销 | 说明 |
| --- | --- | --- |
| MiniFlsIf | 无（仅转发） | — |
| MiniFee | 约 1.8 KB + 标量 | 含 `blockDataBuf`(880) + `writeBuf`(888) + 迁移位图等，随 `MINIFEE_MAX_BLOCK_DATA_SIZE` 与 `MINIFEE_BLOCK_COUNT` 变化 |
| MiniNvm | **约 `MININVM_QUEUE_SIZE × (MININVM_MAX_BLOCK_LENGTH + 元数据)`** | 队列每条目内嵌最大块缓冲，当前 58 × ~888 B ≈ **51 KB**，移植到 RAM 受限平台时须重点评估 |

**RAM 受限时的取舍：**

- 减小 `MININVM_QUEUE_SIZE` 可显著降低 RAM，但此时 `ReadAll`/`WriteAll` 无法一次全量入队（`StartAll` 会因队列满返回 `E_NOT_OK`），需改为分批调用；
- 减小 `MINIFEE_MAX_BLOCK_DATA_SIZE` 会截断超过该值的块，必须同时保证 ≥ 最大块长；
- 若只使用 MiniFee 而不使用 MiniNvm，可省去整个队列开销。

---

## 7. 常见问题与注意事项

1. **异步语义**：所有 Read/Write 返回 `E_OK` 只是“已接受”。必须轮询状态接口，不能立即认为完成。
2. **调度顺序**：必须 `MiniFlsIf → MiniFee → MiniNvm`。漏调或乱序会导致作业永不收敛。
3. **地址基准**：MiniFee 传给 MiniFlsIf 的是偏移量（绝对地址 − `MINIFEE_FLS_BASE`）；`MINIFEE_FLS_BASE` 配错会擦写错误区域。
4. **写缓冲持久性**：MiniFee 的 `writeBuf` / `writeHdrBuf` 是静态上下文成员。底层 Fls 异步持有源指针，**禁止**把它们改成栈上临时数组。
5. **一次编程约束**：RH850 数据 Flash 每个程序单元在两次擦除间只能编程一次。因此块头与数据必须同一次写入，不能先写头再补标志；任何区域都不得二次编程。
6. **CRC-8**：多项式 `0x07`，初始值 `0x00`。簇头校验前 7 字节；块头校验 `BlockNumber + Length + PrevOffset` 共 7 字节。修改格式时须同步。
7. **字节序**：簇头/块头按大端显式序列化，与主机字节序无关；数据区按原始字节搬运，不涉及字节序转换。
8. **BlockId 基准**：MiniFee 从 0 开始，MiniNvm 从 1 开始，映射由描述符维护，勿混用。
9. **配置校验**：`MiniNvm_Init` 校验失败时模块不可用，且不会有显式报错（结果表现为所有请求 `E_NOT_OK`），联调时应先确认配置。
10. **看门狗**：迁移/擦除是长耗时操作，须在轮询循环中喂狗；MiniFee 不负责喂狗。
11. **测试代码**：`MiniNvm_Test` 会擦除并重写数据 Flash 区域，**量产固件必须删除调用**。
12. **内存段**：当前实现使用 `static` 上下文，未使用 AUTOSAR `MemMap` 段。若目标工程强制要求内存段划分，需补充 `MemMap.h` 处理。

---

## 8. 快速自检

工程内保留了开发阶段自检 `MiniNvm_Test()`（`MiniNvm_Test.c/.h`），覆盖：

| 用例 | 验证点 |
| --- | --- |
| PARAM | 非法参数、未初始化拒绝 |
| BLANK | 空白区读取失败处理 |
| BASIC | 单块写后读一致性 |
| SOURCE_COPY | 写请求入队时的数据快照 |
| ALL | `WriteAll` / `ReadAll` 全量一致性 |
| CANCEL | 取消作业后结果置失败 |
| FAILURE | 默认值恢复失败处理 |
| ROTATE | 多次整块重写触发簇迁移，验证数据保全与 Generation 递增 |

自检为破坏性操作，仅用于开发/产线验证；结果见全局变量 `MiniNvm_TestResult`。集成到正式产品前请移除调用。
