# 04 · 配置头文件与 OS 集成（P4）

## 1. MiniFee\_Cfg.h 骨架

见 [config/MiniFee\_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniFee_Cfg.h)。

| 宏                                   | 默认                               | 必须与 FlashDrv 属性对齐？           |
| ----------------------------------- | -------------------------------- | ---------------------------- |
| `MINIFEE_CLUSTER_NUM`               | 4                                | 是（clusterCount）              |
| `MINIFEE_CLUSTER_SIZE`              | 8192                             | 是（clusterSize=擦除单元）          |
| `MINIFEE_PAGE_SIZE`                 | 256                              | 是（pageSize）                  |
| `MINIFEE_MAX_NUM_BLOCKS`            | 64                               | 否（blockMap 静态数组上限；实际块数运行时传入） |
| `MINIFEE_CRC_TYPE`                  | CRC32                            | 否                            |
| `MINIFEE_CRC32_POLY/INIT/XOROUT`    | 0xEDB88320/0xFFFFFFFF/0xFFFFFFFF | 否                            |
| `MINIFEE_GC_THRESHOLD`              | 0                                | 否                            |
| 状态字 `*_STATUS_ERASED/VALID/INVALID` | 0xFFFF/0x5555/0x0000             | 否                            |

- 派生：`MINIFEE_PAGES_PER_CLUSTER = CLUSTER_SIZE/PAGE_SIZE`，`MINIFEE_TOTAL_CAPACITY = CLUSTER_SIZE*CLUSTER_NUM`，`MINIFEE_PAGE_DATA_SIZE = PAGE_SIZE-16`。

- **运行时约束**（`MiniFee_Init(numBlocks)` 校验）：

  - `numBlocks ≤ MINIFEE_MAX_NUM_BLOCKS`。

  - `numBlocks < MINIFEE_PAGES_PER_CLUSTER`（Model A 容量约束）。

  - `MINIFEE_PAGE_SIZE >= 16`（编译期）。

  - `FlashDrv_GetProperty` 与 `CLUSTER_NUM`/`CLUSTER_SIZE`/`PAGE_SIZE` 一致。

## 2. MiniNvm\_Cfg.h 骨架

见 [config/MiniNvm\_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniNvm_Cfg.h)。

| 宏                        | 默认           |
| ------------------------ | ------------ |
| `MININVM_MAX_NUM_BLOCKS` | 块 ID 枚举末项，自动等于块数量 |
| `MININVM_DEFAULT_BYTE`   | 0xFF         |

- 块 ID 和块数量由枚举维护；`MININVM_MAX_NUM_BLOCKS` 不得手工定义或传入初始化。
- 块配置表与 RAM mirror 直接在 `MiniNvm_Cfg.h` 中静态创建，`MiniNvm_Init(void)` 自动使用。

- 集成方示例：

  ```c
    typedef enum {
      MININVM_BLOCK_ID_CONFIG = 0,
      MININVM_BLOCK_ID_COUNTER,
      MININVM_MAX_NUM_BLOCKS
    } MiniNvm_BlockIdType;
    static const MiniNvm_BlockConfigType MiniNvm_BlockConfig[MININVM_MAX_NUM_BLOCKS] = {
      {MININVM_BLOCK_ID_CONFIG, 0x80, 0, TRUE, TRUE},
      {MININVM_BLOCK_ID_COUNTER, 0x40, 0, TRUE, TRUE}
    };
    static uint8 MiniNvm_RamMirror[0xC0];
    MiniNvm_Init();
  ```

- `MiniNvm_Init` 校验：配置表条目数等于 `MININVM_MAX_NUM_BLOCKS`、RAM mirror ≥ 各块 size 之和、`MiniFee_Init` 容量约束满足。

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

1. 实现目标板 `FlashDrv_xxx`（参考 [test/FlashDrv\_Stub.c](file:///d:/WorkSpace/MiniAutosar/test/FlashDrv_Stub.c) 接口）。
2. 按目标 Flash 扇区/页大小，调整 `MiniFee_Cfg.h` 的 `CLUSTER_SIZE/PAGE_SIZE/CLUSTER_NUM`，确保 `pagesPerCluster > 30` 且 pageSize≥块+16。
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

