# 04 · 配置头文件与 OS 集成（P4）

## 1. MiniFee\_Cfg.h 骨架

见 [config/MiniFee\_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniFee_Cfg.h)。

| 宏                                   | 默认                               | 必须与 FlashDrv 属性对齐？           |
| ----------------------------------- | -------------------------------- | ---------------------------- |
| `MINIFEE_CLUSTER_NUM`               | 2                                | 是（clusterCount）              |
| `MINIFEE_CLUSTER_SIZE`              | 8192                             | 是（clusterSize=擦除单元）          |
| `MINIFEE_PAGE_SIZE`                 | 8                                | 是（pageSize=物理最小编程字节数）       |
| `MINIFEE_MAX_BLOCK_SIZE`            | 256                              | 否（单块数据上限=静态工作缓冲约束；须为 PAGE_SIZE 整数倍） |
| `MINIFEE_MAX_NUM_BLOCKS`            | 8                                | 否（blockMap 静态数组上限；= MININVM_MAX_NUM_BLOCKS） |
| `MINIFEE_CRC_TYPE`                  | CRC16                            | 否                            |
| `MINIFEE_CRC16_POLY/INIT/XOROUT`    | 0xA001/0xFFFF/0x0000             | 否                            |
| `MINIFEE_CRC32_POLY/INIT/XOROUT`    | 0xEDB88320/0xFFFFFFFF/0xFFFFFFFF | 否（选 CRC32 时使用）              |
| `MINIFEE_GC_THRESHOLD`              | 0                                | 否                            |
| 状态字 `*_STATUS_ERASED/VALID`        | 0xFFFF/0x5555                    | 否（无 INVALID 态）              |

- 派生：`MINIFEE_PAGES_PER_CLUSTER = CLUSTER_SIZE/PAGE_SIZE`，`MINIFEE_TOTAL_CAPACITY = CLUSTER_SIZE*CLUSTER_NUM`，`MINIFEE_WORK_BUF_SIZE = PAGE_SIZE+2+MAX_BLOCK_SIZE`（MiniFee 内部静态工作缓冲）。

- **编译期约束**：`MINIFEE_PAGE_SIZE >= 8`（header/提交页最小需求）；`CLUSTER_SIZE`、`MINIFEE_MAX_BLOCK_SIZE` 须为 `PAGE_SIZE` 整数倍。

- **运行时约束**（`MiniFee_Init(blockSizes, numBlocks)` 校验）：

  - `numBlocks` > 0 且 ≤ `MINIFEE_MAX_NUM_BLOCKS`，`blockSizes != NULL`。

  - 逐块：`size > 0`、`size % PAGE_SIZE == 0`（FEE block 大小必须是物理写页整数倍）、`size <= MINIFEE_MAX_BLOCK_SIZE`。

  - `Σ blockPages + max(blockPages) <= pagesPerCluster`（Model A 容量约束，blockPages = size/PAGE_SIZE+2）。

  - `FlashDrv_GetProperty` 与 `CLUSTER_NUM`/`CLUSTER_SIZE`/`PAGE_SIZE` 一致。

## 2. MiniNvm\_Cfg.h 骨架

见 [config/MiniNvm\_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniNvm_Cfg.h)。

| 宏                        | 默认           |
| ------------------------ | -------------- |
| `MININVM_MAX_NUM_BLOCKS` | 块 ID 枚举末项，自动等于块数量 |
| `MININVM_BLOCK_x_SIZE`   | 逐块大小宏（配置表与镜像容量的唯一数据源） |
| `MININVM_RAM_MIRROR_SIZE` | 自动=各块 `MININVM_BLOCK_x_SIZE` 之和，勿手填 |
| `MININVM_DEFAULT_BYTE`   | 0xFF         |

- 块 ID 和块数量由枚举维护；`MININVM_MAX_NUM_BLOCKS` 不得手工定义或传入初始化。
- 块配置表与 RAM mirror 直接在 `MiniNvm_Cfg.h` 中静态创建，`MiniNvm_Init(void)` 自动使用。
- 各块大小以 `MININVM_BLOCK_x_SIZE` 宏为唯一数据源，配置表条目引用该宏；`MININVM_RAM_MIRROR_SIZE` 由各块 size 宏求和自动计算，禁止手工填总容量。
- 新增块需同步 4 处：枚举项、`MININVM_BLOCK_x_SIZE` 宏、配置表条目、`MININVM_RAM_MIRROR_SIZE` 求和项。漏加求和项时 `MiniNvm_Init` 的运行时容量校验会拒绝（E_NOT_OK）兜底。

- 集成方示例：

  ```c
    typedef enum {
      MININVM_BLOCK_ID_CONFIG = 0,
      MININVM_BLOCK_ID_COUNTER,
      MININVM_MAX_NUM_BLOCKS
    } MiniNvm_BlockIdType;
    #define MININVM_BLOCK_0_SIZE  ((uint16)0x80u)
    #define MININVM_BLOCK_1_SIZE  ((uint16)0x40u)
    #define MININVM_RAM_MIRROR_SIZE \
        ((uint16)(MININVM_BLOCK_0_SIZE + MININVM_BLOCK_1_SIZE))
    static const MiniNvm_BlockConfigType MiniNvm_BlockConfig[MININVM_MAX_NUM_BLOCKS] = {
      {MININVM_BLOCK_ID_CONFIG, MININVM_BLOCK_0_SIZE, 0, TRUE, TRUE},
      {MININVM_BLOCK_ID_COUNTER, MININVM_BLOCK_1_SIZE, 0, TRUE, TRUE}
    };
    static uint8 MiniNvm_RamMirror[MININVM_RAM_MIRROR_SIZE];
    MiniNvm_Init();
  ```

- `MiniNvm_Init` 校验：RAM 容量兜底（按 size 累加的 offset ≤ `MININVM_RAM_MIRROR_SIZE`；求和宏构造下恒等，用于拦截漏加求和项的人为错误）、`MiniFee_Init` 容量约束满足。配置表长度由数组维度在编译期锁定。

## 3. OS 集成

- **模块与 OS 关系**：MiniNvm/MiniFee 自身 OS 无关、纯 C、无动态内存。无内部并发；非可重入（静态状态）。

- **临界区**：若 bootloader 多任务/中断上下文可能并发调用，需调用方用临界区保护（关中断/锁）。单线程 boot 无需。

- **调用点建议**：

  - 启动：OS 调度就绪后 → `MiniNvm_Init` + `MiniNvm_ReadAll`。

  - 关闭：校验 APP 通过后、跳转前 → `MiniNvm_WriteAll`；或下电前回调。

  - **无需周期调用**（全同步，无后台状态机）。

- **Flash 阻塞**：GC 的擦除与写为同步阻塞，调用方需接受其时长；避免在中断中调用。

## 4. 目标板验证步骤

### 编译配置

1. 实现目标板 `FlashDrv_xxx`（参考 [test/FlashDrv\_Stub.c](file:///d:/WorkSpace/MiniAutosar/test/FlashDrv_Stub.c) 接口；写操作须按 pageSize 对齐且长度为其整数倍）。
2. 按目标 Flash 扇区/页大小，调整 `MiniFee_Cfg.h` 的 `CLUSTER_SIZE/PAGE_SIZE/CLUSTER_NUM/MAX_BLOCK_SIZE`：`PAGE_SIZE` = 物理 Flash 最小编程字节数（如双字编程为 8），各 NvM 块 size 须为其整数倍，且满足 `ΣblockPages+max <= pagesPerCluster`。
3. 在 `MiniNvm_Cfg.h` 配置块表。
4. 编译 `src/MiniFee.c src/MiniNvm.c` + 板端 FlashDrv + 适配层头。

### 验证清单

- [ ] 首启：全 0xFF，`Init+ReadAll` 成功，块为 INVALID。

- [ ] 写块 + `WriteAll`，重启 `Init+ReadAll` 读回一致。

- [ ] 反复重启（断电）不丢历史数据；断电于写中仅丢最新一页。

- [ ] 写满触发 GC：反复写同块直至 GC，读回最新且其它 live 块不丢。

- [ ] CRC 损坏：人为破坏 Flash 数据字节，`ReadAll` 报 `ERR_CRC`。

- [ ] 擦除块 + `WriteAll`，重启后块 NOT\_FOUND。

- [ ] `GetErrorStatus` 各场景标志正确。

> 宿主机侧用 `test/Makefile`（gcc）+ RAM stub 快速验证逻辑，详见 [05\_test\_plan.md](file:///d:/WorkSpace/MiniAutosar/docs/05_test_plan.md)。

