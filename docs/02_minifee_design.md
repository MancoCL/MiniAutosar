# 02 · MiniFee 详细设计（P2）

> 本文于"按 PAGE_SIZE 整页写入 + 逐块变长槽"重构时全面更新（变更记录见 §12）。

## 1. 写入粒度与容量模型（本次重构核心）

- **`MINIFEE_PAGE_SIZE`（S）= 物理 Flash 最小编程字节数**（如 STM32L4 双字编程为 8）。
  - MiniFee 的所有 `FlashDrv_Write` 调用均为**整页写**：地址按 S 对齐、长度恰为 S 的整数倍（实现为逐页各写一次）。
  - 物理编程模型按 **ECC 型 Flash** 假设：每个 S 字节页**只能编程一次**、必须一次写满；不足部分填充 `0xFF`（对擦除态写 0xFF 为空操作，1→0 约束天然满足）。该模型对非 ECC Flash 同样兼容（单次写满是多次写的子集）。
- **FEE block（块槽）大小 = PAGE_SIZE 的整数倍**：块 b 的槽占 `blockPages[b] = blockSizes[b]/S + 2` 页（header 页 1 + 数据页 size/S + 提交页 1）。约束：`blockSizes[b] % S == 0`（运行时校验，违反返回 `MINIFEE_ERR_PARAM`）。
- Fee 管理区 = `MINIFEE_CLUSTER_NUM` 个 cluster，每 cluster `pagesPerCluster = CLUSTER_SIZE/S` 页；**槽不跨 cluster**。

## 2. 块槽布局

```
槽内页     内容
页 0      header 页 : magic[4]="MFEE" + blockId(2) + seq(2) [+ pad 0xFF]
页 1..P-2 数据页    : 数据连续存放 [+ 尾部 pad 0xFF]（size 为 S 整数倍时无 pad）
页 P-1    提交页    : status(2) + dataLen(2) + dataCrc(4) [+ pad 0xFF]
```

- 小端编码；P = blockPages[b]；`dataLen` ≤ `blockSizes[b]`（运行期写入长度可小于容量，尾部填 0xFF）。
- S=8 时 header/提交页恰好占满一页，无 pad；S > 8 时尾部 pad 0xFF。
- **编译期约束**：`MINIFEE_PAGE_SIZE >= 8`（header 页与提交页最小需求）。

## 3. 写次序与状态机（P0 #9，ECC 单次编程友好）

- **写入次序**：① header 页 → ② 数据页（页 1..P-2 逐页）→ ③ 提交页（最后写 = 提交点）。
- 提交页 `status` 只有两种合法状态：`0xFFFF`（ERASED，未提交）与 `0x5555`（VALID，已提交，1→0 友好）。**不存在 INVALID 二次写作废**——ECC 页只能编程一次，无法把 `0x5555` 改写为 `0x0000`。
- **半写/未提交槽**（掉电残留）：提交页 status≠VALID，恢复扫描判为 DIRTY 丢弃 → 满足"可丢最新一页"（P0 #9）。提交页出现其他值（部分写）同样丢弃。
- 旧版本槽的回收：不靠作废位，靠 **seq 新者胜出**（恢复时同块取最大 seq）+ GC 不搬运非映射槽 + cluster 擦除。

## 4. 逻辑→物理映射

- **映射方式**：启动扫描重建 RAM 映射表 `blockMap[blockId] = {valid, corrupt, pageAddr, seq}`，`pageAddr` = 槽首页扁平地址。
- 一个逻辑块任一时刻至多一个"当前槽"（映射指向）；改写 = 在写游标处追加新槽，旧槽成为垃圾等 GC 回收（P0 #5 语义保持）。
- **同块多已提交槽**（提交新槽后崩溃残留、GC 搬运中崩溃残留）：恢复取最大 seq。
- **墓碑槽（tombstone）**：`dataLen==0` 的已提交槽，用于 `MiniFee_EraseBlock`（见 §7）。映射 `valid=TRUE` 指向墓碑；读取时按 NOT_FOUND 处理。
- 损坏槽（VALID 状态但 CRC 损坏）：置 `corrupt` 标志、丢弃；`MiniFee_ReadBlock` 在该块无有效槽时返回 `MINIFEE_ERR_CRC`。

## 5. 磨损均衡（P0 #6，按 cluster）

- **Model A**：所有写都追加到当前 `activeCluster` 的写游标 `writeCursor`（cluster 内页索引）处。
- 写入前检查 `writeCursor + blockPages[b] <= pagesPerCluster`（本次写入的整个槽须落在本 cluster 内）；放不下则先触发 GC，把全部 live 槽搬到别的 cluster，原 cluster 擦除后进入备用池，新 cluster 成为 active。
- cluster 轮转 0→1→…→N-1→0，写入与擦除在多个 cluster 间轮流。
- **容量约束（运行时校验）**：`MiniFee_Init` 校验 `Σ blockPages[b] + max(blockPages) <= pagesPerCluster`，使单 active cluster 能容纳全部 live 块槽 + 最大一个槽的新版本。

## 6. 垃圾回收 GC（P0 #7）

触发条件：本次写入的槽放不进当前 active cluster（`writeCursor + P > pagesPerCluster`）。
流程（见 [src/MiniFee.c](file:///d:/WorkSpace/MiniAutosar/src/MiniFee.c) `gcAndSwitch`）：
1. 选目标 cluster：非 active、且**无任何映射槽指向**（只含垃圾/半搬残留，不含 live 数据）；选定后若非全空则先擦除。
2. 搬运：按块序遍历 `blockMap`，对 `valid==TRUE` 的槽（**含墓碑槽**，保证擦除语义跨 GC 持久；含掉电恢复后的"孤岛槽"，恢复"live 槽全在 active"不变量）读出内容，以原 blockId/seq/dataLen 原样 `writeSlot` 写入 target 的 `tgtCursor` 起始处（墓碑只写 header+提交 2 页，数据页空置但仍按 P 页推进游标），更新 `blockMap[bid].pageAddr`，`tgtCursor += P`。
3. 擦除原 active 以及**其他所有无映射槽且非全空**的 cluster（回收 GC 中断残留的垃圾，碎片自愈；全空 cluster 不擦，避免无意义磨损）。
4. 切换 `activeCluster = target`，`writeCursor = tgtCursor`。
- **一致性/掉电**：搬运逐块提交，中途崩溃时 target 内已提交槽与原槽并存，恢复按最大 seq 取新；未提交半槽判 DIRTY 丢弃（"可丢最新一页"）。
- DIRTY/垃圾槽随 cluster 擦除一并回收。

## 7. 擦除块：墓碑槽（P0 #11）

`MiniFee_EraseBlock(b)`：
- 无映射槽 → 幂等返回 OK。
- 映射槽已是墓碑（提交页 dataLen==0）→ 幂等返回 OK，不再写。
- 否则写入一个墓碑槽：`seq = 旧seq+1`、`dataLen = 0`、只写 header 页 + 提交页（数据页空置）；映射指向墓碑。
- 读取时墓碑 → NOT_FOUND；GC 搬运墓碑 → 擦除语义跨重启/GC 持久。
- 掉电于墓碑提交页写入前 → 墓碑丢失，块回到擦除前数据（"丢最新一次操作"，与 P0 #9 语义一致）。

## 8. 掉电恢复（P0 #9）

`MiniFee_Init` → `scanRebuild`（见 [src/MiniFee.c](file:///d:/WorkSpace/MiniAutosar/src/MiniFee.c)）：
1. 校验 Flash 属性↔配置一致、块表合法（见 §10）。
2. **逐 cluster 混合推进扫描**（页粒度游标 p）：
   - 读页 p 首 4B 无 magic → p += 1（空页/无 header 残留页）；
   - 有 magic → 读 header 页取 blockId；越界则 p += 1（无法定界，逐页跳过，人工介入场景）；
   - 合法则 P = blockPages[bid]，若 `p+P` 超出 cluster 则 p += 1；否则整槽校验（header magic/blockId → 提交页 status → dataLen ≤ 容量 → 数据页 CRC）：
     - 提交页 status≠VALID → 半写槽，p += P 跳过（可丢最新一页）；
     - dataLen 非法或 CRC 损坏 → 置 `corrupt`，p += P；
     - 通过 → 按 isNewer 更新映射（墓碑同样参与 seq 竞争），p += P。
   - 记录每 cluster 推进终点 `endPage[c]`。
3. 定位 active：取"全局最大 seq 有效槽"所在 cluster。
4. 定位 writeCursor = `endPage[active]`（active 内已有槽序列——含半写槽——之后的第一个可用页；半写槽整体跳过，不复用其已编程页）。

## 9. 校验 CRC（P0 #10）

- 可配置 CRC16（当前默认）/CRC32；多项式可配置。CRC16 反射型，多项式 `0xA001`、初值 `0xFFFF`、异或 `0x0000`；CRC32 反射型，多项式 `0xEDB88320`（【假设】，与 config 现状对齐：当前选择 CRC16）。
- **覆盖范围**：header 页整页（S 字节，含 pad）+ `dataLen`(2B) + `data`(dataLen B)，即槽内 `[header 页][dataLen][data]` 的连续拼接（物理上 dataLen 在提交页，由软件拼接计算）；不覆盖 `dataCrc`/`status` 自身。
- 计算时机：写入时计算并随提交页存储；读取/扫描/GC 搬运校验时重算比对。

## 10. 初始化与配置校验

`MiniFee_Init(blockSizes, numBlocks)`：
1. `FlashDrv_Init` + `GetProperty`，逐项校验 `PAGE_SIZE/CLUSTER_SIZE/CLUSTER_NUM` 与配置一致。
2. 校验 `numBlocks > 0` 且 ≤ `MINIFEE_MAX_NUM_BLOCKS`；`blockSizes != NULL`。
3. 逐块校验：`size > 0`、`size % S == 0`（FEE block 大小必须是 PAGE_SIZE 整数倍）、`size <= MINIFEE_MAX_BLOCK_SIZE`（内部静态工作缓冲约束）。
4. 派生 `blockPages[b] = size/S + 2`；校验 `Σ blockPages + max(blockPages) <= pagesPerCluster`。
5. `scanRebuild` 重建映射；任一步失败返回对应错误码且模块保持未初始化。

## 11. 配置宏清单（占位默认值，均【假设】）

| 宏 | 默认 | 说明 |
|---|---|---|
| `MINIFEE_CLUSTER_NUM` | 2 | ≥2；建议 ≥3（见 docs/06 风险：2 cluster 下 GC 中断后可能无全空备用） |
| `MINIFEE_CLUSTER_SIZE` | 8192 | 须与 Flash 擦除单元对齐且为 S 整数倍 |
| `MINIFEE_PAGE_SIZE` | 8 | 物理 Flash 最小编程字节数；≥8 |
| `MINIFEE_MAX_BLOCK_SIZE` | 256 | 单块数据上限（静态工作缓冲 = S+2+该值）；须为 S 整数倍 |
| `MINIFEE_MAX_NUM_BLOCKS` | 8 | blockMap 静态数组上限；= MININVM_MAX_NUM_BLOCKS |
| `MINIFEE_CRC_TYPE` | CRC16 | 可改 CRC32 |
| `MINIFEE_CRC16_POLY/INIT/XOROUT` | 0xA001/0xFFFF/0x0000 | 反射 |
| `MINIFEE_CRC32_POLY/INIT/XOROUT` | 0xEDB88320/0xFFFFFFFF/0xFFFFFFFF | 反射 |
| `MINIFEE_GC_THRESHOLD` | 0 | 仅写满触发（预留） |
| 状态字 `ERASED/VALID` | 0xFFFF/0x5555 | 1→0 友好；无 INVALID |

派生：`MINIFEE_PAGES_PER_CLUSTER`、`MINIFEE_TOTAL_CAPACITY`、`MINIFEE_WORK_BUF_SIZE`（= S+2+MAX_BLOCK_SIZE）。

## 12. 边界与错误场景

| 场景 | 处理 |
|---|---|
| 全 0xFF 首启 | 无有效槽，active=0/writeCursor=0；ReadBlock→NOT_FOUND |
| 块槽放不进 active | 写前触发 GC |
| 无可用目标 cluster（全被占用/含映射槽） | MINIFEE_ERR_FULL（需人工格式化） |
| 掉电于槽写中途 | 提交页未写，恢复整槽丢弃（丢最新一页）；已编程页不复用 |
| 掉电于墓碑提交前 | 墓碑丢失，块回到擦除前数据（丢最新一次操作） |
| CRC 损坏 | corrupt 标志，ReadBlock→ERR_CRC |
| blockId 越界的 header 页 | 无法定界，逐页跳过（人工介入场景） |
| 数据页首 4 字节恰为 "MFEE" | 小概率误判槽首页；整槽校验（提交页 status）可拦截大部分，残余风险见 docs/06 |
| FlashDrv 写/擦失败 | 返回 ERR_FLASH，状态可能不一致但恢复可自愈 |

## 13. 变更记录

- **v2（本次）**：`MINIFEE_PAGE_SIZE` 语义改为物理最小编程单元（默认 8）；块存储改为逐块变长槽（`size/S+2` 页），所有 Flash 写为整页单次编程；删除 INVALID 二次写作废，改为墓碑槽 + seq 竞争；GC 改为按映射搬运 live 槽（含墓碑/孤岛）并回收无映射槽的垃圾 cluster；`MiniFee_Init` 增加逐块 size 校验（S 整数倍、≤MAX_BLOCK_SIZE）；`MiniFee_GetPageDataSize` → `MiniFee_GetBlockDataSize(blockId)`，新增 `MiniFee_GetWritePageSize()`。
- **v1**：统一页槽（一页 = header+data+CRC+status，PAGE_SIZE=256），三步提交 + INVALID 作废。
