# 05 · 测试方案（P5）

## 1. 测试架构（宿主侧为主）

- **Flash 驱动 mock**：[test/FlashDrv_Stub.c](file:///d:/WorkSpace/MiniAutosar/test/FlashDrv_Stub.c) 用 RAM 数组模拟 Flash（总容量 32KB），强制 1→0 写约束，擦除置 0xFF。`FlashDrv_Init` 不擦除（保留内容跨"重启"），由 `FlashDrv_Stub_Reset` 负责全新首启。
- **故障注入**：
  - `FlashDrv_Stub_FailStatusWrites(n)`：令接下来 n 次 2 字节写（status 提交/作废）失败，模拟提交前掉电。
  - `FlashDrv_Stub_FailAnyWrites(n)`：任意写失败。
  - `FlashDrv_Stub_FailErases(n)`：擦除失败。
  - `FlashDrv_Stub_SetByte/GetByte`：绕过约束注入损坏/检视。
- **掉电注入**：通过 `FailStatusWrites(1)` 在提交步失败并再次 `MiniFee_Init` 模拟重启恢复。
- **CRC 工具**：直接调用 `MiniFee_CalcCrc`（静态）经破坏对比验证；通过 `SetByte` 破坏数据触发。
- **运行**：`make`（gcc）。无 gcc 时可手动 `gcc -std=c99 -Wall -Wextra -Iinclude -Iconfig -Itest src/*.c test/*.c -o t && ./t`。

## 2. MiniFee 测试用例（[test/MiniFee_Test.c](file:///d:/WorkSpace/MiniAutosar/test/MiniFee_Test.c)）

| 编号 | 前置 | 步骤 | 预期 | 覆盖 P0 |
|---|---|---|---|---|
| TC-F-01 | 全 0xFF | Reset+Init(8)；ReadBlock(0) | Init OK；NOT_FOUND；属性正确；numBlocks=8 | #9,#12 |
| TC-F-02 | 空 Flash | WriteBlock(0,128)→ReadBlock(0) | OK；数据一致；len=128 | #5,#10 |
| TC-F-02b | 空 Flash | 各变长块 WriteBlock→ReadBlock | OK；len 各等于该块配置 size | #5,#10 |
| TC-F-03 | 空 Flash | WriteBlock(0,v1)→WriteBlock(0,v2)→Read | v2；旧页作废 | #5,#6 |
| TC-F-04 | 空 Flash | 写 block1/2 各一次+block0×30 填满 active(32)→再写 block0 触发 GC→读 block0/1/2 | GC OK；三块最新值保持 | #6,#7 |
| TC-F-05 | 写过 block1 | 破坏数据区字节→Init→ReadBlock(1) | ERR_CRC | #10 |
| TC-F-06 | 写过 block2 | EraseBlock(2)→ReadBlock→再 EraseBlock | NOT_FOUND；幂等 OK | #11 |
| TC-F-07 | 写过 block3 v1 | FailStatusWrites(1)→WriteBlock(3,v2) 失败→Init→ReadBlock(3) | 返回 FLASH 错；恢复读回 v1（丢最新页） | #9 |
| TC-F-08 | Init | 非法参数（越界/NULL/len 过大） | ERR_PARAM | — |
| TC-F-09 | Init | Init(0)/Init(MAX+1)/Init(pagesPerCluster) | ERR_PARAM | — |

## 3. MiniNvm 测试用例（[test/MiniNvm_Test.c](file:///d:/WorkSpace/MiniAutosar/test/MiniNvm_Test.c)）

| 编号 | 前置 | 步骤 | 预期 | 覆盖 P0 |
|---|---|---|---|---|
| TC-N-01 | 全 0xFF | Init(cfg,8,ram)→ReadBlock(0)→GetErrorStatus→ReadAll→ReadBlock(0) | 未 ReadAll 返回 E_NOT_OK+UNINIT；ReadAll 后 INVALID | #11,#13 |
| TC-N-02 | Init+ReadAll | WriteBlock(0)→ReadBlock(0)→MiniFee_ReadBlock(0) | RAM 一致；Flash 未写（NOT_FOUND） | #13 |
| TC-N-03 | Init+ReadAll | WriteBlock(0)→WriteAll→重启 Init+ReadAll→ReadBlock(0) | 值跨重启保持 | #13 |
| TC-N-04 | 写过块1 | EraseNvBlock(1)→WriteAll→重启 Init+ReadAll→ReadBlock(1) | E_NOT_OK（已擦） | #11 |
| TC-N-05 | 写过块2+WriteAll | 破坏数据→重启 Init+ReadAll→GetErrorStatus(2)→ReadBlock(2) | ERR_CRC；E_NOT_OK | #10,#11 |
| TC-N-06 | 写块0/1 | FailAnyWrites(1)→WriteAll→GetErrorStatus(0)→再 WriteAll | E_NOT_OK+ERR_WRITE；重试 E_OK | #11 |
| TC-N-07 | Init+ReadAll | 写 5 块→WriteAll→重启→读 5 块 | 全部一致（变长 size） | #13,#15 |
| TC-N-08 | Init | GetBlockSize(0/1/2)+GetNumBlocks | 各块 size 正确；块数=8 | #15 |
| TC-N-09 | Init | Init(NULL/0/>MAX/RAM不足) | E_NOT_OK | — |

## 4. 覆盖追溯

- 每个 P0 条目至少一个用例覆盖（#1~#20 见 [06_acceptance_review.md](file:///d:/WorkSpace/MiniAutosar/docs/06_acceptance_review.md)）。
- 掉电场景 TC-F-07 覆盖"丢最新一页可接受"预期。
