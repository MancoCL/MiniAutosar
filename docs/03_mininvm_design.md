# 03 · MiniNvm 详细设计（P3）

## 1. 块配置（P0 #15）

- 见 [config/MiniNvm\_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniNvm_Cfg.h)。

- **块数量与各块大小均不在配置头文件中硬编码**，由集成方在启动时构造配置表数组并传给 `MiniNvm_Init`。

```c
typedef struct {
    uint16  id;        // 块 ID（= 索引）
    uint16  size;      // 块大小（逐块指定，须 ≤ MINIFEE_PAGE_DATA_SIZE）
    uint16  ramOffset; // RAM 镜像偏移（填 0，Init 时按 size 累加计算）
    boolean readAll;  // 参与 ReadAll
    boolean writeAll; // 参与 WriteAll
} MiniNvm_BlockConfigType;
```

- 集成方在启动时构造 `MiniNvm_BlockConfigType cfg[N]` 数组（逐块指定 `size`/`readAll`/`writeAll`），连同 RAM 镜像缓冲一起传入：
  ```c
  MiniNvm_Init(cfg, N, ramBuf, sizeof(ramBuf));
  ```
- `MiniNvm_Init` 内部按各块 `size` 累加计算 `ramOffsets[]`，校验总和不超过传入的 RAM 缓冲大小。
- `MiniNvm_GetBlockSize(blockId)` / `MiniNvm_GetNumBlocks()` 供调用方查询实际块大小与块数。
- `MININVM_MAX_NUM_BLOCKS`（= 64）仅用于模块内部 `dirty`/`blockState`/`blockErr` 静态数组声明上限，不限制集成方的实际块数。

### Flash 占用估算（给定页大小）

- 页数据区 = `PAGE_SIZE-16 = 240`，各块 `size` 须 ≤ 此值。

- N 块各占 1 页 → N 页数据。`pagesPerCluster=32`，须 `N < 32`，单 cluster 容纳全部 live 块 + 空闲。

- 总容量 `4 × 8192 = 32 KB`，余量充足。

## 2. RAM 镜像与写回

### 2.1 上电 Init + ReadAll

- `Init(blockCfg, numBlocks, ramBuf, ramSize)`：接收集成方块配置表+RAM缓冲，按 `size` 累加计算 `ramOffsets[]`，镜像填缺省（`MININVM_DEFAULT_BYTE=0xFF`），块置 `UNINIT`；调 `MiniFee_Init(numBlocks)` 完成扫描恢复。

- `ReadAll`（见 [src/MiniNvm.c](file:///d:/WorkSpace/MiniAutosar/src/MiniNvm.c)）：逐块（readAll=TRUE）`MiniFee_ReadBlock(id, mirror, &len)`：

  - `MINIFEE_OK`：装镜像，数据区不足块大小处填缺省，状态 `VALID`。

  - `MINIFEE_ERR_NOT_FOUND`：装缺省，状态 `INVALID`，无错误（首启）。

  - `MINIFEE_ERR_CRC`：装缺省，状态 `INVALID`，错误标志 `ERR_CRC`。

  - 其它：装缺省，状态 `INVALID`，`ERR_READ`。

### 2.2 运行期 WriteBlock（同步，不落 Flash）

- `memcpy(dataBuf→镜像)` + `dirty[id]=TRUE` + 状态 `VALID` + 清错误。

- **不调 MiniFee、不碰 Flash**（P0 #13）。

### 2.3 WriteAll 触发与回写

- 触发时机：离开 boot / 跳转 APP 前 / 下电前。

- 流程：逐块（writeAll=TRUE）若 `dirty[id]`：

  - 状态 `VALID` → `MiniFee_WriteBlock(id, mirror, size)`；成功清 dirty。

  - 状态 `INVALID` → `MiniFee_EraseBlock(id)`；成功清 dirty。

  - 失败：置 `ERR_WRITE`/`ERR_ERASE`，**保留 dirty 待下次重试**。

- 全部成功→E\_OK；否则 E\_NOT\_OK。

### dirty 管理

- `static boolean dirty[MININVM_MAX_NUM_BLOCKS]`；Init 清零；WriteBlock/EraseNvBlock 置位；WriteAll 成功清位。

## 3. 读路径

- `MiniNvm_ReadBlock`：从 RAM 镜像返回，不访问 Flash。

- `UNINIT`（未 ReadAll）→ E\_NOT\_OK（不拷贝）。

- `INVALID`/`VALID` → 拷镜像；返回 `VALID? E_OK : E_NOT_OK`。

## 4. 擦除 EraseNvBlock

- 仅 Native（P0 #12）：镜像置缺省 + dirty + 状态 `INVALID`。

- 真正"擦 Flash 页"（作废 MiniFee 页）发生在 WriteAll（INVALID+dirty → `MiniFee_EraseBlock`）。

- 幂等语义：对未写块 EraseNvBlock 后 WriteAll，`MiniFee_EraseBlock` 因无有效页返回 OK。

## 5. 状态与错误

- 块状态：`UNINIT / VALID / INVALID`。

- `GetErrorStatus` 返回位掩码（[MiniNvm.h](file:///d:/WorkSpace/MiniAutosar/include/MiniNvm.h)）：
  `ERR_NONE/ CRC/ READ/ WRITE/ ERASE/ UNINIT`；UNINIT 块额外或上 `ERR_UNINIT`。

- 与 MiniFee 返回码映射见 [01\_architecture.md](file:///d:/WorkSpace/MiniAutosar/docs/01_architecture.md) §5。

- 说明：`GetErrorStatus` 表征错误标志；块是否可读以 `ReadBlock` 返回为准（INVALID 块无错误标志但不可读）。

## 6. 与 MiniFee 交互时序

| 操作                      | MiniNvm             | → MiniFee | → FlashDrv  |
| ----------------------- | ------------------- | --------- | ----------- |
| Init                    | MiniFee\_Init       | 扫描        | Read        |
| ReadAll(每块)             | MiniFee\_ReadBlock  | 读页+校验     | Read        |
| WriteAll(VALID dirty)   | MiniFee\_WriteBlock | 写页/GC     | Write/Erase |
| WriteAll(INVALID dirty) | MiniFee\_EraseBlock | 作废页       | Write       |
| 运行期 WriteBlock/Erase    | （不调 MiniFee）        | —         | —           |

失败传播：MiniFee 返回码 → MiniNvm 块错误标志，dirty 保留。

## 7. 启动/关闭时序

```
启动：  [bootloader 入口] → (OS 就绪) → MiniNvm_Init → MiniNvm_ReadAll
运行期： MiniNvm_ReadBlock / WriteBlock / EraseNvBlock / GetErrorStatus（均 RAM 层）
关闭：  [校验 APP/准备跳转] → MiniNvm_WriteAll → 跳转 APP / 下电
```

- 无需周期任务（全同步）。若需确保 WriteAll 落盘，务必在不可恢复跳转前调用。

## 8. 边界场景

| 场景                    | 行为                                                   |
| --------------------- | ---------------------------------------------------- |
| 未初始化 Flash（全 0xFF）    | ReadAll：每块 NOT\_FOUND→装缺省，INVALID，无错误                |
| 首启                    | 同上                                                   |
| 块数据损坏                 | ReadAll：CRC→装缺省，INVALID，ERR\_CRC                     |
| MiniFee 返回 FLASH 错    | ReadAll：ERR\_READ；WriteAll：ERR\_WRITE/ERASE，dirty 保留 |
| 未 ReadAll 即 ReadBlock | E\_NOT\_OK（UNINIT）                                   |
| 未 WriteAll 即跳转        | RAM 改动丢失（符合 dirty 未回写预期）                             |

