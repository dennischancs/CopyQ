# AI 会话规则

> **文档类型：权威事实源**（约束定义）
> 行为规则只在 `SPEC.md`。架构只在 `ARCH.md`。

## 输入三件套

1. `ARCH.md`
2. `SPEC.md` 里和本任务相关的小节
3. `src/tests/configsynctests.cpp` 是桌面同步的验收依据

不要为了「对齐」去改测试让它变绿。

## 禁止

| # | 禁止 | 原因 |
|---|------|------|
| 1 | 修改、删除、跳过 `configsynctests` 的断言 | 验证和实现要分开 |
| 2 | 把 `COPYQ_CONFIG_SYNC_STUB` 加回生产代码 | 绿灯已经证明这个变量不能关掉实现 |
| 3 | 新增 zip、HTTP、JSON 依赖 | `ARCH.md` |
| 4 | 新建 Android/iOS 工程并标完成 | `todo.md` 写的是未完成 |
| 5 | 在日志或错误详情里放密码 | SPEC R6 |
| 6 | 改 gnome 扩展或翻译文件来消除 Gate 的 sql-concat 误报 | 那些文件不是本任务 |

## 结束时要写清

- 改了哪些文件
- SPEC 里哪几条有测试证据，哪几条没有
- 没做到的事。没有就写「无」

## 怎样算做完

1. `build/copyq-tests testConfigSync` 通过
2. SPEC 第 5 节有红灯和绿灯记录
3. `gate_check.sh` 的结果已写入 `REVIEW-CHECKLIST.md`，FAIL 有豁免理由
4. `todo.md` 里移动端仍是未完成
5. `spec_tracker.py verify` 只用于真正有证据的任务

缺一条就不是做完。
