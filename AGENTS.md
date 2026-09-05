# AGENTS.md

面向 bootloader 的轻量级 AUTOSAR 风格 NVRAM 持久化组件（MiniNvm/MiniFee/FlashDrv 分层）。纯 C99、静态内存、全同步、无回调、OS 无关，默认非可重入（并发保护由调用方负责）。文档、注释、commit message 均使用中文。本文件是权威工作流文件，适用于本工程中的每一次分析、修改、测试和代码生成请求。

## 项目基本情况

- 分层与调用链（自上而下单向）：`bootloader → MiniNvm → MiniFee → FlashDrv → 硬件 Flash / RAM stub`。禁止跨层：MiniNvm 只经 MiniFee 访问 Flash，bootloader 不直调 MiniFee/FlashDrv。
  - MiniNvm：块配置、RAM 镜像、块状态、dirty 管理、`ReadAll`/`WriteAll` 写回。
  - MiniFee：Flash EEPROM 模拟、页管理、块到页映射、CRC、磨损均衡、GC、掉电恢复。
  - FlashDrv：底层抽象，仅负责读、页编程、cluster 擦除和属性查询；MiniFee 不得直接依赖具体硬件实现。
- 关键设计约束：
  - `MiniNvm_WriteBlock`/`EraseNvBlock` 只更新 RAM 镜像 + 置 dirty，不碰 Flash；真实落盘只发生在 `MiniNvm_WriteAll`。
  - Flash 写只允许 1→0；掉电恢复允许丢弃最新半写页（P0 #9，TC-F-07 预期即"丢 v2 保 v1"）。
- 目录职责：`docs/` 权威文档；`include/` 公共接口与标准类型；`config/` 编译期配置宏；`src/` MiniFee 与 MiniNvm 实现；`test/` gcc 宿主测试、RAM Flash stub 与故障注入。
- `config/*.h` 的容量参数（CLUSTER_SIZE/PAGE_SIZE/CLUSTER_NUM、CRC、块表等）是【假设】占位默认值，不能擅自当成已确认的产品需求；`MiniFee_Init` 会校验配置与 FlashDrv 属性一致性。

## 命令

唯一验证手段是 `test/` 下的 gcc 宿主测试（无 lint/typecheck/codegen）：

```
cd test && make        # 编译并运行（make build 仅编译 / make run 仅运行 / make clean）
# 无 make 时（仓库根目录，等价单行）：
gcc -std=c99 -Wall -Wextra -Iinclude -Iconfig -Itest src/*.c test/*.c -o t && ./t
```

- 无测试框架：`test/*.c` 为普通函数 + `CHECK` 宏（`g_pass`/`g_fail` 计数），单一二进制一次跑全部用例，秒级完成；没有按用例过滤的机制，聚焦验证就是跑全套。
- 通过标准：进程退出码 0，输出末尾 `FAIL: 0`。
- 本 Windows 主机 PATH 上没有 make/gcc（已实测），Makefile 为 POSIX 风格（`./$(BIN)`），需 MinGW/MSYS2/WSL；没有可用工具链时不得声称测试已运行。

## 权威文档映射

修改对应模块前，必须先阅读并维护相应文档（实现以文档为唯一依据）：

- 总体架构、调用链、公共 API、错误传播：`docs/01_architecture.md`
- MiniFee 页格式、状态机、映射、GC、恢复、CRC、Flash 适配：`docs/02_minifee_design.md`
- MiniNvm 块表、RAM 镜像、ReadAll/WriteAll、dirty、状态、错误处理：`docs/03_mininvm_design.md`
- 配置宏、目标板集成、OS 集成、容量参数：`docs/04_config_integration.md`
- 测试策略、测试用例、故障注入、覆盖追溯：`docs/05_test_plan.md`
- 验收结论、P0 需求、风险、遗留项：`docs/06_acceptance_review.md`

`.trae/`（已 gitignore）是辅助笔记，不是权威。

## 强制文档先行流程

任何需要修改、增加或删除代码的请求，都必须严格执行以下顺序：

1. 先定位受影响的模块、接口、配置和测试，并阅读对应的 `docs/*.md`。
2. 在修改任何 `*.c`、`*.h`、Makefile 或测试实现之前，先修改对应的设计/集成/测试文档，写清楚目标行为、接口变化、数据结构、约束、错误处理、边界情况和验证方式。若现有设计已覆盖需求，也要在文档中补充本次变更记录或明确更新后的规则。
3. 文档修改完成后，以修改后的文档为唯一实现依据生成或修改对应的代码文件；不得先写代码再倒填文档，也不得让代码与文档出现已知不一致。
4. 若需求影响多个层次，按依赖顺序更新所有相关文档：先架构/API，再详细设计，再配置/集成和测试计划，最后视情况更新验收评审。
5. 代码实现必须保持现有命名、分层和静态内存风格，禁止绕过 `MiniNvm`/`MiniFee` 直接访问 FlashDrv，除非文档先明确批准架构变化。
6. 代码完成后补充或调整对应测试，并运行最小相关验证；至少执行 `test/Makefile` 的 gcc 测试或等价的 C99 编译运行命令。文档中的测试预期必须与实际结果一致。
7. 如果文档与用户新要求冲突，先更新文档并指出冲突和影响，再实现新要求；如果需求无法从现有文档确定，先询问，不要凭猜测改变持久化格式、Flash 布局、公开 API 或配置契约。

## 工作输出要求

- 开始修改前，先简要说明受影响的文档和代码路径，以及本次变更假设。
- 每次涉及代码的变更，都要在结果中列出先更新了哪些文档、随后生成/修改了哪些代码和测试。
- 不做与当前需求无关的重构，不擅自修改用户已有改动，不添加未经文档说明的功能。
- 发现测试、配置或文档与实现不一致时，优先修正文档基线，再修正实现和测试。

## 已知坑

- `docs/05_test_plan.md` TC-N-01 的 `Init(cfg,8,ram)` 是旧签名：现行 `MiniNvm_Init(void)`，块配置表与 RAM 镜像静态定义在 `config/MiniNvm_Cfg.h`；块数经 `MiniFee_Init(numBlocks)` 运行时传入。
- `MiniNvm_Cfg.h` 在头文件内定义 `static` 数组（配置表 + RAM 镜像），随 `MiniNvm.h` 传播到每个包含者，各 TU 持有独立副本；MiniNvm API 实际使用 `MiniNvm.c` 内那份。
- 测试 stub：`FlashDrv_Init` 不擦除（RAM 模拟 Flash 内容跨"重启"保留，用于掉电恢复测试）；全新首启必须先调 `FlashDrv_Stub_Reset()`。故障注入：`FlashDrv_Stub_FailStatusWrites/FailAnyWrites/FailErases`；破坏/检视字节：`SetByte/GetByte`。
