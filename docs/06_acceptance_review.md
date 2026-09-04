# 06 · 验收评审（P7）

## 1. P0 需求逐项核查表

| P0 # | 需求要点 | 结论 | 证据位置 |
|---|---|---|---|
| 1 | 仅 boot 使用 | 满足 | 架构定位 [01](file:///d:/WorkSpace/MiniAutosar/docs/01_architecture.md) §1；模块无 APP 共享约定 |
| 2 | 平台无关+Flash 抽象 | 满足 | [FlashDrv.h](file:///d:/WorkSpace/MiniAutosar/include/FlashDrv.h)；MiniFee 经抽象层 |
| 3 | 标准 OS、模块 OS 无关 | 满足 | 纯 C/无动态内存/无 OS 调用；[04](file:///d:/WorkSpace/MiniAutosar/docs/04_config_integration.md) §3 |
| 4 | 多 cluster（≥2） | 满足 | `MINIFEE_CLUSTER_NUM=4`；[02](file:///d:/WorkSpace/MiniAutosar/docs/02_minifee_design.md) §1 |
| 5 | 块↔页一一映射 | 满足 | [02](file:///d:/WorkSpace/MiniAutosar/docs/02_minifee_design.md) §3；TC-F-02/03 |
| 6 | 磨损均衡轮转 | 满足 | Model A 轮转 [02](file:///d:/WorkSpace/MiniAutosar/docs/02_minifee_design.md) §4；TC-F-04 |
| 7 | 标准 GC | 满足 | `gcAndSwitch` [02](file:///d:/WorkSpace/MiniAutosar/docs/02_minifee_design.md) §5；TC-F-04 |
| 8 | 访问链路 NvM→Fee→Flash | 满足 | [01](file:///d:/WorkSpace/MiniAutosar/docs/01_architecture.md) §1；boot 不直调 |
| 9 | 可丢最新一页 | 满足 | 提交次序+恢复丢弃半写页 [02](file:///d:/WorkSpace/MiniAutosar/docs/02_minifee_design.md) §2/§6；TC-F-07 |
| 10 | 简化软件 CRC | 满足 | CRC32/CRC16 可配 [02](file:///d:/WorkSpace/MiniAutosar/docs/02_minifee_design.md) §7；TC-F-05/N-05 |
| 11 | 服务集齐全 | 满足 | [MiniNvm.h](file:///d:/WorkSpace/MiniAutosar/include/MiniNvm.h) Read/Write/Erase/GetErrorStatus/ReadAll/WriteAll |
| 12 | 仅 Native 单副本 | 满足 | 无 Redundant/DataSet；[03](file:///d:/WorkSpace/MiniAutosar/docs/03_mininvm_design.md) §4 |
| 13 | RAM 镜像+ReadAll/WriteAll 写回 | 满足 | [03](file:///d:/WorkSpace/MiniAutosar/docs/03_mininvm_design.md) §2；TC-N-01/02/03 |
| 14 | 写保护不支持 | 满足 | 未实现（设计明确不提供） |
| 15 | 块数与大小可逐块指定 | 满足 | [MiniNvm_Cfg.h](file:///d:/WorkSpace/MiniAutosar/config/MiniNvm_Cfg.h)；Init 接收配置表+块数+RAM缓冲；TC-N-07/08/09 |
| 16 | 资源简洁可移植 | 满足 | 静态缓冲、无动态内存、可移植类型 |
| 17 | 命名 MiniNvm_/MiniFee_ | 满足 | 全部 API |
| 18 | 底层仅抽象必要操作 | 满足 | FlashDrv 仅读/写/擦/属性 |
| 19 | 仅同步 | 满足 | 无回调/状态机；全函数返回即完成 |
| 20 | 交付物齐全 | 满足 | 设计文档/代码/配置头/测试（本套） |

无"不满足且未说明"条目。

## 2. 关键设计风险清单

| 风险 | 影响 | 缓解/接受理由 |
|---|---|---|
| 磨损均衡有效性 | 单 active 写满才轮转，集中擦写当前/备用 cluster | 仍多 cluster 轮转，符合 P0 #6；boot 写入稀疏可接受。TC-F-04 验证轮转发生 |
| GC 一致性与掉电窗口 | GC 搬运中崩溃可能残留两 cluster 同块有效页 | 恢复按最大 seq 取新；半写页丢弃。可丢最新一页（P0 #9）接受 |
| WriteAll 前掉电丢失范围 | 未 WriteAll 则 RAM 改动全部丢失 | 符合 dirty 写回策略预期；务必在跳转前调 WriteAll（[04](file:///d:/WorkSpace/MiniAutosar/docs/04_config_integration.md) §3 已提示） |
| CRC 强度与覆盖范围 | CRC32 软件实现，覆盖页头+数据 | 满足 P0 #10 简化要求；非安全认证级 |
| seq 回绕 | uint16 seq 在 ~65k 次写后回绕，`isNewer` 在 32768 边界附近判定异常 | bootloader 写次数远低于此，接受；如需可改 uint32 |
| Model A 容量 | 要求 pagesPerCluster > 块数 | 编译期断言保护；当前 32>30 |
| 中途 GC 崩溃残留 dirty cluster | 极端情况下备用 cluster 非全空→GC 返回 FULL | 仅"丢最新一页"场景之外，概率低；返回 FULL 提示人工格式化 |

## 3. 遗留问题与下一步

- **需用户拍板的参数**（均为【假设】占位，已给默认值待确认）：`CLUSTER_SIZE`(8192)、`PAGE_SIZE`(256)、`CLUSTER_NUM`(4)、CRC 类型与多项式、缺省字节、块表（每块 size/offset/参与标志）。
- **需补充的测试**（目标板侧）：真实 Flash 擦除时序下的 GC 阻塞时长测量；断电注入在真机上的"丢最新一页"复现；多任务并发下的临界区验证（若适用）。
- **可选增强**（未在 P0 要求内，列以待定）：Redundant/DataSet 块、写保护、异步/通知机制——本期按 P0 明确不做。
