# 01 · 总体架构与接口设计（P1）

## 1. 分层架构与数据流

```
+---------------------------+
|  bootloader（调用方）       |
+-------------+-------------+
              | 同步调用 MiniNvm_xxx
              v
+---------------------------+
|  MiniNvm（NVRAM 管理）      |  RAM 镜像 / 块表 / ReadAll·WriteAll 写回
+-------------+-------------+
              | 同步调用 MiniFee_xxx
              v
+---------------------------+
|  MiniFee（Flash EEPROM 模拟）|  页管理 / 磨损均衡 / GC / 掉电恢复 / CRC
+-------------+-------------+
              | 同步调用 FlashDrv_xxx
              v
+---------------------------+
|  FlashDrv 抽象层            |  读 / 写（页编程）/ 擦除（cluster）/ 属性查询
+-------------+-------------+
              |
              v
        硬件 Flash  /  RAM stub（测试）
```

- **调用方向**：自上而下单向。bootloader 不得绕过 MiniNvm 直调 MiniFee/FlashDrv（P0 #8）。
- **各层职责**：
  - bootloader：上电调 `MiniNvm_Init` + `MiniNvm_ReadAll`；运行期读写块；跳转 APP/下电前调 `MiniNvm_WriteAll`。
  - MiniNvm：维护块表与 RAM 镜像、dirty 管理、错误状态；所有 Flash 访问经 MiniFee。
  - MiniFee：在 Flash 上模拟 EEPROM，管理页/簇、磨损均衡、GC、掉电恢复、CRC。
  - FlashDrv：屏蔽硬件差异，仅提供读/写/擦/属性查询。
- 全同步、无回调、纯 C、无动态内存、OS 无关（P0 #3/#19）。

## 2. MiniNvm 对外 API（bootloader 调用）

见 [include/MiniNvm.h](file:///d:/WorkSpace/MiniAutosar/include/MiniNvm.h)。全部同步语义：

| API | 语义 | 返回 |
|---|---|---|
| `MiniNvm_Init(void)` | 建块表、镜像填缺省、置 UNINIT；内部调 `MiniFee_Init` 完成扫描恢复。不读盘。 | E_OK / E_NOT_OK |
| `MiniNvm_ReadBlock(blockId, dataBuf)` | 从 RAM 镜像读（不访问 Flash）。VALID→E_OK；INVALID→E_NOT_OK（仍拷镜像）；UNINIT→E_NOT_OK。 | E_OK / E_NOT_OK |
| `MiniNvm_WriteBlock(blockId, dataBuf)` | 仅更新 RAM 镜像 + dirty + 置 VALID；**不落 Flash**。 | E_OK / E_NOT_OK |
| `MiniNvm_EraseNvBlock(blockId)` | 镜像置缺省 + dirty + 置 INVALID。真实擦除由 WriteAll 完成。 | E_OK / E_NOT_OK |
| `MiniNvm_ReadAll(void)` | 上电逐块经 `MiniFee_ReadBlock` 读入镜像+CRC；无效块装缺省置 INVALID。 | E_OK / E_NOT_OK(严重) |
| `MiniNvm_WriteAll(void)` | 逐 dirty 块经 MiniFee 回写：VALID→WriteBlock，INVALID→EraseBlock；成功清 dirty。 | E_OK / E_NOT_OK |
| `MiniNvm_GetErrorStatus(blockId, &err)` | 返回块错误状态位掩码。 | E_OK / E_NOT_OK |

> **WriteBlock 语义澄清（P0 #13）**：`WriteBlock` 不立即落盘，只更新 RAM 镜像并标记 dirty。真实 Flash 写入发生在 `WriteAll`。这满足"运行期写只更新 RAM 镜像、WriteAll 统一回写"的硬约束。

## 3. MiniFee 对外 API（仅 MiniNvm 调用）

见 [include/MiniFee.h](file:///d:/WorkSpace/MiniAutosar/include/MiniFee.h)。

| API | 语义 | 返回 |
|---|---|---|
| `MiniFee_Init(void)` | 校验 Flash 属性↔配置一致；扫描重建块→页映射。 | MINIFEE_OK / ERR_FLASH / ERR_PARAM |
| `MiniFee_ReadBlock(blockId, dest, &len)` | 读块最新有效页+CRC。无页→NOT_FOUND；损坏→CRC。 | MINIFEE_OK / NOT_FOUND / CRC / FLASH / PARAM |
| `MiniFee_WriteBlock(blockId, src, len)` | 分配新页→写头+数据+CRC→提交 VALID→作废旧页；写满触发 GC。 | OK / PARAM / FULL / FLASH |
| `MiniFee_EraseBlock(blockId)` | 作废该块有效页（幂等）。 | OK / PARAM / FLASH |
| `MiniFee_GetPageDataSize(void)` | 页数据区大小。 | uint16 |
| `MiniFee_GetClusterCount(void)` | cluster 数。 | uint16 |

## 4. FlashDrv 抽象层接口

见 [include/FlashDrv.h](file:///d:/WorkSpace/MiniAutosar/include/FlashDrv.h)。

| API | 说明 |
|---|---|
| `FlashDrv_Init` | 初始化驱动 |
| `FlashDrv_Read(addr, len, dest)` | 读 |
| `FlashDrv_Write(addr, len, src)` | 页编程；只能 1→0，否则 FLASH_ERR_PROG |
| `FlashDrv_EraseCluster(clusterIdx)` | 擦除整 cluster（置 0xFF） |
| `FlashDrv_GetProperty(&prop)` | 查询 pageSize/clusterSize/clusterCount/totalCapacity/writeGranularity/eraseAtomicity |

地址模型：Fee 管理区为扁平空间 `[0, totalCapacity)`；cluster i 基址 = `i*clusterSize`，页 j 基址 = `clusterBase + j*pageSize`。

## 5. 错误码体系与传播路径

```
FlashDrv 返回码            MiniFee 返回码            MiniNvm 块错误状态
FLASH_OK            ─→   MINIFEE_OK          ─→   (无错误，状态 VALID)
FLASH_ERR_PROG/FAIL ─→   MINIFEE_ERR_FLASH   ─→   ERR_WRITE / ERR_READ
FLASH_ERR_ERASE     ─→   MINIFEE_ERR_FLASH   ─→   ERR_ERASE
(页 CRC 损坏)        ─→   MINIFEE_ERR_CRC     ─→   ERR_CRC
(无有效页/首启)      ─→   MINIFEE_ERR_NOT_FOUND─→  (INVALID，无错误标志)
(写满无落脚点)       ─→   MINIFEE_ERR_FULL    ─→   ERR_WRITE
```

- MiniFee 把 FlashDrv 错误统一映射为 `MINIFEE_ERR_FLASH`；CRC 损坏单独 `MINIFEE_ERR_CRC`；无页 `MINIFEE_ERR_NOT_FOUND`。
- MiniNvm 在 ReadAll/WriteAll 时把 MiniFee 返回码转译为块错误状态位（见 [MiniNvm.h](file:///d:/WorkSpace/MiniAutosar/include/MiniNvm.h) `MiniNvm_ErrorStatusType`）。

## 6. 配置框架（总体，细节见 04）

- [config/MiniFee_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniFee_Cfg.h)：cluster 数、页大小、cluster 大小、CRC 类型与多项式、页头字段魔数/状态字、GC 阈值。
- [config/MiniNvm_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniNvm_Cfg.h)：块数量、块大小、RAM 对齐、缺省字节、块配置表（块 ID/大小/镜像偏移/ReadAll·WriteAll 参与标志）。
- 所有量化参数为编译期宏（【假设】占位默认值），禁止运行时动态配置；`MiniFee_Init` 校验配置↔FlashDrv 属性一致性（P4 对齐要求）。

## 7. RAM 镜像策略时序

```
上电  ─ MiniNvm_Init ─→ MiniFee_Init(扫描恢复) ─→ 镜像填缺省，块置 UNINIT
      ─ MiniNvm_ReadAll ─→ 逐块 MiniFee_ReadBlock→镜像+CRC；无效块装缺省置 INVALID
运行期 ─ MiniNvm_WriteBlock(id,buf) ─→ memcpy→镜像 + dirty + VALID（不碰 Flash）
      ─ MiniNvm_EraseNvBlock(id)   ─→ 镜像填缺省 + dirty + INVALID
下电/跳转APP ─ MiniNvm_WriteAll ─→ 逐 dirty 块 MiniFee 回写（VALID→Write / INVALID→Erase）→ 清 dirty
```

调用点建议：`Init+ReadAll` 在 bootloader 启动早期、调度系统就绪后；`WriteAll` 在校验 APP 完成后、跳转前，或下电钩子中。模块自身 OS 无关，无需周期任务（全同步）。
