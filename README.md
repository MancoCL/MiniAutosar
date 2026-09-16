# MiniAutosar

面向 **Bootloader / BootServices** 场景的轻量级非易失存储栈，由三个自下而上的模块组成：

```text
应用 / 上层业务
    │  MiniNvm API（逻辑块、请求队列、结果）
    ▼
MiniNvm        —— RAM Block 管理、单块/多块异步请求排队与结果维护
    │  MiniFee API（逻辑 Block、记录、CRC、迁移、磨损均衡）
    ▼
MiniFee        —— EEPROM 仿真：Flash 记录组织、追加写入、簇迁移
    │  MiniFlsIf API（异步 Flash 适配）
    ▼
MiniFlsIf      —— 平台适配层（当前为 RH850 Fls MCAL）
    │
    ▼
Flash
```

三个模块均为**异步状态机**：`Read/Write` 类接口返回 `E_OK` 仅表示请求被接受，必须周期调用各自的 `MainFunction` 推进，最终结果经状态查询接口获取。全栈不阻塞、不含 `MemMap` 依赖、不直接耦合具体 Flash 驱动。

## 主要特性

- **异步非阻塞**：状态机分拍推进，适配 Boot 阶段看门狗约束。
- **追加式写入 + 簇轮换**：同一块每次写入追加新记录，旧记录由整簇迁移回收，天然实现磨损均衡。
- **RAM 块目录**：`MiniFee` 在 RAM 维护“块号 → 最新记录偏移”目录，冷启动构建一次；之后单块读仅 1 次数据读、追加写 0 次旧数据读。
- **一次性批量读写**：`ReadAll` / `WriteAll` 单遍扫描/连续追加全部块，避免逐块重复选簇。
- **数据去重**：`MiniNvm_WriteBlock` 与 RAM buffer 比对，内容一致不置脏、不写入；`WriteAll` 只落盘脏块。
- **仅一次编程**：块头与数据同一次写入，无 Valid/Invalid 标志页，不做 1→0 二次编程（适配 RH850 数据 Flash）。
- **CRC-8 校验 + 大端序列化**：簇头与块头显式校验与字节序，主机无关；新记录遮蔽旧记录。
- **平台隔离**：仅 `MiniFlsIf.c` 与平台绑定，移植只需替换该文件。

## 目录结构

```text
MiniAutosar/
├── Codes/
│   ├── Config/                     可变配置（块表、簇表、RAM buffer）
│   │   ├── MiniFee_Cfg.h / .c      MiniFee：地址基址、写粒度、块配置、簇配置
│   │   └── MiniNvm_Cfg.h / .c      MiniNvm：块描述符与各块 RAM buffer
│   ├── Memory/
│   │   ├── MiniFlsIf/              平台适配层（MiniFlsIf.h / .c）
│   │   ├── MiniFee/                存储核心（MiniFee.h / .c）
│   │   └── MiniNvm/                逻辑块与请求队列（MiniNvm.h / .c）
│   └── Test/                       开发阶段自检（MiniNvm_Test.h / .c）
└── Docs/
    ├── MiniFlsIf_SDD.md            MiniFlsIf 详细设计
    ├── MiniFee_SDD.md              MiniFee 详细设计
    ├── MiniNvm_SDD.md              MiniNvm 详细设计
    ├── MiniStorage_集成手册.md      三模块集成 / 移植手册
    └── 变更记录.md                  本轮重构变更说明
```

## 快速开始

### 1. 初始化

```c
MiniNvm_Init();     /* 内部依次调用 MiniFee_Init → MiniFlsIf_Init */
```

若只使用 MiniFee 而不使用 MiniNvm，则调用 `MiniFee_Init()`。

### 2. 周期调度

必须按固定顺序、每层各调用一次 MainFunction（推荐 1 ms 或更快周期），并在循环中喂看门狗：

```c
MiniFlsIf_MainFunction();   /* 推进底层 Flash */
MiniFee_MainFunction();     /* 推进存储状态机 */
MiniNvm_MainFunction();     /* 推进请求队列 */
```

> MiniNvm **不会**代调用 MiniFee / MiniFlsIf 的 MainFunction，漏调或乱序会导致作业永不收敛。

### 3. 典型用法

```c
uint8 buf[MININVM_MAX_BLOCK_LENGTH];

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
    /* r 为 MININVM_REQ_OK / NOT_OK */
}

/* 单块读 */
if (MiniNvm_ReadBlock(1u, buf) == E_OK) { /* 同上轮询 */ }

/* 全量读写（一次性路径，不经队列） */
MiniNvm_WriteAll();
MiniNvm_ReadAll();
```

运行期修改块内容请使用 `MiniNvm_WriteRam`（会置脏），再由 `MiniNvm_WriteAll` 落盘；直接经 `MiniNvm_GetRamBuffer` 改写 RAM buffer 不会置脏、不会被写入。

## 关键设计

### 物理布局（`MiniFee`）

```text
Cluster(簇) = [8B 簇头][记录1][记录2]...[记录N][0xFF 空白...]
簇头   = Magic(4) + Generation(3, 大端) + CRC-8(1)
记录   = 8B 块头 + align(Length) 数据
块头   = BlockNumber(3) + Length(4) + CRC-8(1)   （大端）
```

- **活动簇** = Magic 正确且 Generation 最大的簇；无有效簇时写请求首次激活簇 0。
- **追加写入**：新记录写到活动簇 `headerEnd`，旧记录不删除；RAM 块目录记录每块最新记录位置。
- **迁移与磨损均衡**：空间不足或记录损坏时，擦除备用簇，按目录把每块最新记录搬到备用簇（本次要写的块直接写新数据），写新簇头（Generation+1）后切换活动簇；源簇保留以便断电回退，待其作为目标时再擦除。

### RAM 与性能

| 模块 | 主要 RAM 开销 |
| --- | --- |
| MiniFlsIf | 无（仅转发） |
| MiniFee | `jobBuf`（读/写缓冲共用 `union`，取较大者）+ `4 × 块数`（块目录）+ 固定标量 |
| MiniNvm | `DataBuf`（最大块长）+ `队列容量 × 队列条目(仅元数据)` + `块数 × (结果 + 脏标记)` + 固定标量 |

队列与读/写缓冲均不再内嵌每块数据，RAM 显著低于初版实现。

## 示例配置

`Codes/Config` 下为**示例配置**，集成时请按实际工程替换。

| 配置 | 示例值 | 说明 |
| --- | --- | --- |
| `MINIFEE_FLS_BASE` | `0xFF200000` | 地址基址，须与底层 Fls 一致 |
| `MINIFEE_VIRTUALPAGE_SIZE` | `4` | Flash 最小写单元（物理对齐粒度） |
| `MINIFEE_MAX_BLOCK_DATA_SIZE` | `32` | 单块数据缓冲上限，须 ≥ 最大块长 |
| 示例块 | 3 个（标志 4B / 配置 10B / 数据 32B） | `MINIFEE_BLOCK_EXAMPLE_*`，演示含非对齐长度 |
| 簇表 | `0xFF200000/0x2000`、`0xFF202000/0x2000` | 双簇轮换，数量须 ≥ 2 |
| `MININVM_QUEUE_SIZE` | `8` | 请求队列容量，用户自定义，约束并发单块请求数（`ReadAll`/`WriteAll` 不经队列） |

修改块表 / 簇表 / 上限时，须同步更新 `MiniFee_Cfg` 与 `MiniNvm_Cfg`，详见集成手册。

## 文档

- [MiniFlsIf 软件详细设计](Docs/MiniFlsIf_SDD.md)
- [MiniFee 软件详细设计](Docs/MiniFee_SDD.md)
- [MiniNvm 软件详细设计](Docs/MiniNvm_SDD.md)
- [MiniFee / MiniNvm / MiniFlsIf 集成手册](Docs/MiniStorage_集成手册.md)
- [变更记录](Docs/变更记录.md)

## 自检

`Codes/Test/MiniNvm_Test.c` 提供开发阶段功能自检 `MiniNvm_Test()`，覆盖参数、空白、单块读写、源数据快照、全量读写、取消、簇迁移/翻页等用例，结果见全局变量 `MiniNvm_TestResult`。

> **自检为破坏性操作**：会擦除并重写数据 Flash 区域，量产固件必须移除调用。集成前请参考《集成手册》第 5 节移植步骤与检查清单。

## 注意事项

- **异步语义**：`E_OK` 仅表示“已接受”，必须轮询状态接口。
- **调度顺序**：`MiniFlsIf → MiniFee → MiniNvm`，不可省略或乱序。
- **地址基准**：`MINIFEE_FLS_BASE` 配错会擦写错误区域。
- **写缓冲持久性**：`writeBuf` / `writeHdrBuf` 是静态上下文成员，底层 Fls 异步持有源指针，禁止改为栈上临时数组。
- **一次编程约束**：块头与数据必须同一次写入，任何区域不得二次编程。
- **移植点**：仅 `MiniFlsIf.c` 与平台绑定；若底层驱动为同步接口，须在适配层内实现异步状态机。

> 版本与历史变更见[变更记录](Docs/变更记录.md)；当前为 V1.1。`Docs/MiniFee_SDD.md`、`Docs/MiniNvm_SDD.md` 正文待同步至 V1.1 设计。
