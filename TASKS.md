# TASKS — 执行状态

> **文档类型：执行状态**（可重建，允许滞后，不允许矛盾）
> 权威事实源是 `SPEC.md` 与 `ARCH.md`。本文件只记录进展。
> 同一条规则不要在本文件里重新定义。

## 状态图

```
todo -> doing -> review -> verified
          \-> blocked -> (resume) -> doing
any -> dropped
```

- 「完成」= `verified`。`review` 通过不等于完成。
- `blocked` 必须写原因，且同一任务连续 blocked 2 次要触发复盘。

## 任务表

| ID | 任务 | 验收判据（唯一） | 状态 | 依赖 | 备注 |
|------|------|------|------|------|------|
| T1 | 冻结规格与文档 | SPEC.md 含 R1-R15 与红绿记录；prd 与 dev-plan 只引用不另写句子 | verified | - | 2026-10-16 发布前复核 |
| T2 | 看见未实现红灯 | COPYQ_CONFIG_SYNC_STUB=1 时 testConfigSync 因 NOT_IMPLEMENTED 失败，而不是编译或端口绑定失败 | verified | T1 | 红灯记录在 SPEC 第 5 节，桩已删除，不能重放 |
| T3 | 删桩后桌面同步变绿 | 变量仍为 1 时 build/copyq-tests testConfigSync 退出码 0 | verified | T2 | 2026-10-16 复跑 25 passed，退出码 0 |
| T4 | 移动端只列未完成项 | todo.md 列出 Android 与 iOS 不能全量移植的功能，且仓库没有移动端工程 | review | T1 | 清单已写。不标 verified，避免被读成移动端已移植 |

## 原子性自查

每个任务在 `doing` 之前确认四条：

1. 单次会话内可完成
2. 有单一明确的验收判据
3. 可独立提交而不破坏其他任务
4. 依赖关系已显式声明

## Git 提交纪律

- 人做/ 人监督的重构：允许与功能交织（《重构》2.4）
- **agent 自主改动：重构与功能必须分离**，重构提交单独标注
  （依据：15,451 次实测中 53.9% 的 agent 重构出现在无重构意图的提交中）

## Gate 历史

- [2026-10-16 15:42] FAIL — second run after TASKS.md: FAIL 42 WARN 2 PASS 3 SKIP 2. Same pre-existing exemptions. Retrospective appended to .spec-history.log

- [2026-10-16 15:41] FAIL — pre-existing sql-concat false positives and unittest discover; feature command testConfigSync is 25 passed. Exemptions in REVIEW-CHECKLIST.md

见 `.spec-history.log`。用 `spec_tracker.py gate --result ...` 记录。

## 复盘触发条件

- 同一任务连续 2 次 blocked
- 需求中途实质变更
- Gate 连续 2 次 FAIL
