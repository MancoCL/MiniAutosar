# 02 · MiniFee 详细设计（P2）

## 1. Flash 布局与页结构

- Fee 管理区 = `MINIFEE_CLUSTER_NUM` 个 cluster，每 cluster `MINIFEE_PAGES_PER_CLUSTER = CLUSTER_SIZE/PAGE_SIZE` 个页。
- **页结构**（紧凑、小端编码，见 [src/MiniFee.c](file:///d:/WorkSpace/MiniAutosar/src/MiniFee.c)）：

```
偏移   字段          大小   说明
0      magic[4]      4     "MFEE"（0x4D 0x46 0x45 0x45），标识已写页
4      blockId       2     块 ID
6      seq           2     写序号（同块取最大）
8      dataLen       2     有效数据长度
10     data[]        DATA  数据区（DATA = PAGE_SIZE-16 = 240）
OFF_DATACRC  dataCrc 4     CRC32（覆盖 magic+blockId+seq+dataLen+data）
OFF_STATUS   status  2     页状态字
```
- `OFF_DATACRC = 10 + DATA = PAGE_SIZE-6`，`OFF_STATUS = PAGE_SIZE-2`，页头 10B + 页尾 6B = 16B 开销。

## 2. 页状态机（Flash 1→0 友好）

```
ERASED(0xFFFF) ──提交──→ VALID(0x5555) ──作废──→ INVALID(0x0000)
```
- 三态码两两之间的位变化均为 1→0：`0xFFFF→0x5555`（清位）、`0x5555→0x0000`（清位）、`0xFFFF→0x0000`（清位），无需擦除即可推进。
- **提交次序**：先写页头+数据，再写 `dataCrc`，最后写 `status=VALID`（提交）。提交前的页 status 仍为 `0xFFFF`（ERASED）。
- **半写/未提交页**（掉电残留）：status≠VALID，恢复时判为 DIRTY/INVALID 丢弃 → 满足"可丢最新一页"（P0 #9）。
- 任何非三态值（部分写）→ DIRTY，丢弃。

## 3. 逻辑→物理映射

- **映射方式**：启动扫描重建 RAM 映射表 `blockMap[blockId] = {valid, corrupt, pageAddr, seq}`。
- **块↔页一一映射**（P0 #5）：一个逻辑块任一时刻至多一个 VALID 页；改写即写新页 + 作废旧页。
- **同块多 VALID 页**（提交新页后、作废旧页前崩溃残留）：恢复时取最大 seq。
- 损坏页（VALID 状态但 CRC 损坏）：置 `corrupt` 标志、丢弃；`MiniFee_ReadBlock` 在该块无有效页时返回 `MINIFEE_ERR_CRC`，便于上层区分。

## 4. 磨损均衡（P0 #6）

- **Model A**：所有写都落到当前 `activeCluster`，写游标 `writeCursor` 顺序推进。
- 写满触发 GC，把有效页搬到备用 cluster，擦除原 cluster，备用 cluster 成为新 active。
- cluster 轮转 `0→1→…→N-1→0`，写入在多个 cluster 间轮流。
- **容量约束（运行时校验）**：`MiniFee_Init(numBlocks)` 校验 `numBlocks < MINIFEE_PAGES_PER_CLUSTER`，使单 active cluster 能容纳全部 live 块 + 1 空闲。

## 5. 垃圾回收 GC（P0 #7）

触发条件：`writeCursor >= pagesPerCluster`（当前写 cluster 写满，下次写前）。
流程（见 [src/MiniFee.c](file:///d:/WorkSpace/MiniAutosar/src/MiniFee.c) `gcAndSwitch`）：
1. 选目标 cluster：遍历非 active cluster，选首个"全空"（无 magic）者；无则 `MINIFEE_ERR_FULL`。
2. 搬运：扫描 active 内所有页，对 `status==VALID` 且 CRC 通过的页，按 `blockId/seq/dataLen` 原样写入 target 顺序页，更新 `blockMap[bid].pageAddr`。
3. 擦除原 active cluster（`FlashDrv_EraseCluster`）。
4. 切换 `activeCluster = target`，`writeCursor = 搬运页数`。
- **一致性/掉电**：搬运中崩溃可能残留两 cluster 各有同块有效页（恢复取最大 seq 解决）；若崩溃发生在擦除前，目标 cluster 已有新页、原 cluster 旧页仍在，恢复后以 seq 大者为准。半写页判 DIRTY 丢弃（"可丢最新一页"）。
- INVALID/DIRTY 页随 cluster 擦除一并回收。

## 6. 掉电恢复（P0 #9）

`MiniFee_Init` → `scanRebuild`（见 [src/MiniFee.c](file:///d:/WorkSpace/MiniAutosar/src/MiniFee.c)）：
1. 校验 Flash 属性↔配置一致。
2. 遍历全部 cluster/页：读整页；
   - 无 magic → 空页，跳过；
   - status≠VALID → 半写/作废，跳过（可丢最新一页）；
   - status==VALID：校验 dataLen 与 CRC；损坏→置 `corrupt`，丢弃；通过→按 blockId 更新映射（取最大 seq）。
3. 定位 active：取"全局最大 seq 有效页"所在 cluster。
4. 定位 writeCursor：active 内首个无 magic（空）页索引；全满则 = pagesPerCluster。

## 7. 校验 CRC（P0 #10）

- 可配置 CRC32（默认）/CRC16；多项式可配置。默认 CRC32 反射型，多项式 `0xEDB88320`，初值/异或 `0xFFFFFFFF`（【假设】）。
- 覆盖范围：`magic + blockId + seq + dataLen + data`（即 `[0, OFF_DATA+dataLen)`）；不覆盖自身 `dataCrc` 与 `status`。
- 计算时机：写入时计算并存储；读取/扫描时重算比对。

## 8. 同步写实现

`MiniFee_WriteBlock` 阻塞完成（见 [src/MiniFee.c](file:///d:/WorkSpace/MiniAutosar/src/MiniFee.c)）：
1. 参数校验；若 `writeCursor>=pagesPerCluster` 内联触发 GC。
2. `newSeq = (valid? seq+1 : 0)`；`newAddr = pageAddr(active, writeCursor)`。
3. `writePage`：写页头+数据 → 写 dataCrc → 提交 status=VALID（三步 FlashDrv_Write）。
4. 更新 blockMap；`writeCursor++`；作废旧页（`status=INVALID`）。
- 擦除耗时：GC 的 `FlashDrv_EraseCluster` 同步阻塞，调用方需接受阻塞时长（bootloader 可接受）。

## 9. Flash 驱动适配

- MiniFee 仅调 FlashDrv 读/写/擦/属性，不碰硬件（P0 #18）。
- 错误处理：FlashDrv 错误 → `MINIFEE_ERR_FLASH` 返回，不重试（由上层决定）。
- 写约束依赖 FlashDrv 强制 1→0；MiniFee 设计的状态机与提交次序天然适配。

## 10. 配置宏清单（占位默认值，均【假设】）

| 宏 | 默认 | 说明 |
|---|---|---|
| `MINIFEE_CLUSTER_NUM` | 4 | ≥2 |
| `MINIFEE_CLUSTER_SIZE` | 8192 | 须与 Flash 扇区对齐；使 pagesPerCluster=32 |
| `MINIFEE_PAGE_SIZE` | 256 | ≥块+16 |
| `MINIFEE_MAX_NUM_BLOCKS` | 64 | blockMap 静态数组上限；实际块数运行时传入 |
| `MINIFEE_CRC_TYPE` | CRC32 | 可改 CRC16 |
| `MINIFEE_CRC32_POLY` | 0xEDB88320 | 反射 |
| `MINIFEE_GC_THRESHOLD` | 0 | 仅写满触发 |

## 11. 边界与错误场景

| 场景 | 处理 |
|---|---|
| 全 0xFF 首启 | 无有效页，active=0/writeCursor=0；ReadBlock→NOT_FOUND |
| cluster 写满 | 下次写前 GC |
| 无全空备用 cluster | MINIFEE_ERR_FULL（需人工格式化） |
| 掉电于写页中途 | 未提交页 status≠VALID，恢复丢弃（丢最新一页） |
| CRC 损坏 | corrupt 标志，ReadBlock→ERR_CRC |
| blockId 越界页 | 扫描跳过 |
| FlashDrv 写/擦失败 | 返回 ERR_FLASH，状态可能不一致但恢复可自愈 |
