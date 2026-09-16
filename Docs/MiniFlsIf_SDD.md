# MiniFlsIf 软件详细设计

| 项目 | 内容 |
| --- | --- |
| 模块名称 | MiniFlsIf |
| 所属层级 | BSW / SystemServices / BootServices |
| 上层模块 | MiniFee |
| 下层模块 | Fls MCAL（或其他 Flash 驱动） |
| 设计版本 | V1.1 |
| 作者 | Manco |
| 描述 | 初版发布：MiniFee 与底层 Flash 驱动之间的异步平台适配层 |

## 1. 模块概述

MiniFlsIf 是 MiniFee 与底层 Flash 驱动之间的异步平台适配层。它把底层具体 Flash 驱动（当前为 RH850 Fls MCAL）的初始化、异步读/写/擦除、状态查询和周期推进封装为一组固定接口，使 MiniFee 无需包含任何平台相关头文件，也不需要感知底层驱动类型。

MiniFlsIf 本身**不实现任何 Flash 算法**，只做转发与语义保持。真正的逻辑块管理、数据格式、校验、迁移和磨损均衡全部由 MiniFee 负责。

## 2. 模块职责与边界

**职责：**

- 转发 Flash 初始化、反初始化、读取、写入和擦除请求；
- 提供底层任务状态（`MemIf_StatusType`）与任务结果（`MemIf_JobResultType`）查询；
- 提供底层 Flash 周期推进接口 `MiniFlsIf_MainFunction`；
- 保持底层异步调用语义（`E_OK` 仅表示请求被接受，不代表完成）；
- 隔离平台相关头文件（如 `Fls.h`、`SchM_Fls.h`）与实现细节。

**不负责：**

- 逻辑块管理、地址映射、数据格式、校验、迁移、磨损均衡；
- 重试、同步等待、喂狗或调度；
- 修改地址、长度或数据内容。

## 3. 软件架构与依赖

```text
MiniFee
   |
   | MiniFlsIf 异步接口
   v
MiniFlsIf
   |
   | 平台相关 Flash 接口（Fls_Init / Fls_Read / Fls_Write / Fls_Erase / Fls_GetStatus / ...）
   v
Fls MCAL 或其他 Flash 驱动
```

| 依赖 | 说明 |
| --- | --- |
| `Common.h` | `uint8`、`E_OK` / `E_NOT_OK`、`NULL_PTR` 等基础类型与宏 |
| `MemIf_Types.h` | `MemIf_StatusType`、`MemIf_JobResultType` 定义 |
| `Fls.h` | 底层 Fls MCAL 接口（仅实现文件依赖） |
| `SchM_Fls.h` | Fls 的调度/临界区声明（仅实现文件依赖） |

## 4. 对外接口

| 接口 | 原型 | 说明 |
| --- | --- | --- |
| `MiniFlsIf_Init` | `uint8 MiniFlsIf_Init(void)` | 初始化底层 Flash 驱动（当前调用 `Fls_Init(FlsConfigSet)`），固定返回 `E_OK` |
| `MiniFlsIf_DeInit` | `uint8 MiniFlsIf_DeInit(void)` | 反初始化底层 Flash 驱动，固定返回 `E_OK` |
| `MiniFlsIf_Read` | `uint8 MiniFlsIf_Read(uint32 offset, uint8* buf, uint32 len)` | 发起异步读取，转发 `Fls_Read` |
| `MiniFlsIf_Write` | `uint8 MiniFlsIf_Write(uint32 offset, const uint8* buf, uint32 len)` | 发起异步写入，转发 `Fls_Write` |
| `MiniFlsIf_Erase` | `uint8 MiniFlsIf_Erase(uint32 offset, uint32 len)` | 发起异步擦除，转发 `Fls_Erase` |
| `MiniFlsIf_GetStatus` | `MemIf_StatusType MiniFlsIf_GetStatus(void)` | 查询底层任务状态（`MEMIF_UNINIT / IDLE / BUSY / BUSY_INTERNAL`） |
| `MiniFlsIf_GetJobResult` | `MemIf_JobResultType MiniFlsIf_GetJobResult(void)` | 查询最近一次任务结果（`MEMIF_JOB_OK / FAILED / PENDING / CANCELED / ...`） |
| `MiniFlsIf_MainFunction` | `void MiniFlsIf_MainFunction(void)` | 周期推进底层 Flash 异步任务，转发 `Fls_MainFunction` |

**配置项说明：** MiniFlsIf **没有自有配置宏**，接口本身不携带任何项目确定值；相关参数全部由目标工程决定：

| 项 | 性质 | 取值依据 |
| --- | --- | --- |
| `offset` 基准 | 平台相关 | 传入的 `offset` 一律为“绝对地址 − Flash 基址”；该基址由 MiniFee 的 `MINIFEE_FLS_BASE` 决定，必须与目标驱动的地址基址一致 |
| Fls 配置集（如 `FlsConfigSet`） | 平台相关 | 地址范围、擦除单元、写单元由目标工程 Fls 配置决定，必须与 MiniFee 的 `MiniFee_ClusterConfig[]`（地址/长度）和 `MINIFEE_VIRTUALPAGE_SIZE`（写粒度）匹配 |

**语义约定：**

- 读、写、擦除返回 `E_OK` 只表示请求已被底层接受，不表示操作已完成；
- 操作完成状态必须通过 `MiniFlsIf_GetStatus`（是否仍 `MEMIF_BUSY`）与 `MiniFlsIf_GetJobResult`（是否 `MEMIF_JOB_OK`）确认；
- `offset` 为相对 Flash 基址的偏移量，具体基址由平台 Fls 配置决定。

## 5. 运行时约束

1. MiniFlsIf 不在接口内部阻塞等待 Flash 完成；
2. MiniFlsIf 不修改地址、长度和数据内容；
3. MiniFlsIf 不调用 MiniFee 或 MiniNvm 接口，不存在反向依赖；
4. 平台相关依赖只出现在实现文件或平台工程配置中；
5. 周期推进由系统调度器或上层测试显式调用，推荐调用顺序：
   `MiniFlsIf_MainFunction` → `MiniFee_MainFunction` → `MiniNvm_MainFunction`。

## 6. 移植说明（映射到其他平台）

MiniFlsIf 是三个模块中唯一与平台绑定的部分。移植到其他 Flash 驱动/平台时，只需修改 `MiniFlsIf.c`：

1. 替换 `#include "Fls.h"` / `#include "SchM_Fls.h"` 为目标驱动头文件；
2. 将 `MiniFlsIf_Init` 中的 `Fls_Init(FlsConfigSet)` 替换为目标驱动的初始化调用；
3. 将 `MiniFlsIf_Read/Write/Erase` 转发到目标驱动的读写擦接口（若目标驱动为同步接口，需自行封装为异步状态机，否则会破坏 MiniFee 的非阻塞假设）；
4. 将 `MiniFlsIf_GetStatus/GetJobResult` 映射到目标驱动的状态/结果查询（若目标驱动无 `MemIf` 风格状态，需要在适配层内部维护状态机并映射为 `MemIf_StatusType` / `MemIf_JobResultType`）；
5. 将 `MiniFlsIf_MainFunction` 映射到目标驱动的周期推进函数。

`MiniFlsIf.h` 中的接口原型、`MiniFee` 与 `MiniNvm` 均**不需要修改**。

## 7. 注意事项

- **必须保持异步语义**：若把同步阻塞的 Flash 驱动直接转发，MiniFee 的状态机将退化为阻塞等待，破坏 Boot 阶段的实时性与看门狗约束；
- **`offset` 基准统一**：MiniFlsIf 接收的是偏移量，MiniFee 传入的偏移量已经减去了 `MINIFEE_FLS_BASE`，移植时务必确认目标驱动的地址基准一致；
- **状态/结果查询时序**：发起操作后，只有 `GetStatus` 返回 `MEMIF_IDLE` 时 `GetJobResult` 才有意义；MiniFee 已按此顺序处理；
- **`MiniFlsIf_DeInit` 当前为空实现**：若目标平台需要关闭 Flash 控制器或时钟，应在此补充。

## 8. 验证要求

- 初始化/反初始化接口调用正确；
- 读、写、擦除请求能够正确转发到目标驱动；
- Pending、成功、失败状态能够正确查询；
- 周期调用能够推进底层异步任务直至 `MEMIF_IDLE`；
- 接口不发生同步阻塞或数据修改。
