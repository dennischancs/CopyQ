# 开发计划

> 执行状态只看 [TASKS.md](TASKS.md)。行为只看 [SPEC.md](SPEC.md)。本文件是阅读顺序，不是第二份规格。

## 顺序

1. **T1 冻结文档。** `SPEC.md`、`ARCH.md`、`prd.md`、本文件、清单 schema、`todo.md`。规则句子只放在 SPEC。
2. **T2 看见红灯。** `src/tests/configsynctests.cpp` 在 `COPYQ_CONFIG_SYNC_STUB=1` 时失败，原因是未实现。记录在 SPEC 第 5 节。
3. **T3 桌面同步变绿。** 删掉三个实现文件里的桩，再跑 `build/copyq-tests testConfigSync`。2026-10-16 是 25 passed，退出码 0。变量仍然设着，用来证明它关不掉实现。
4. **T4 移动端只做清单。** 不建工程。不能移植的功能写在 `todo.md`，状态保持未完成。

T2 依赖 T1 的契约。T3 依赖 T2 的红灯记录。T4 不依赖桌面代码变绿，但不要把它标成 verified。

## 实际发生的顺序

测试和实现在同一次工作里都已经存在，然后才补文档。这不符合 skill 里「先冻结再写代码」的理想顺序。补救是：红灯用桩制造，绿灯在删桩之后，文档把这个缺口写明。不要把过程说成先有规格后有代码。

编译能跑之前还修了类型名、`isSymLink`、测试头文件和 `m_tabBackup` 成员。这些是编译修复，SPEC 里单独写了，不算红灯证据。

## 提交

发布前于 2026-10-16 复跑验收命令：`QT_QPA_PLATFORM=offscreen COPYQ_CONFIG_SYNC_STUB=1 build/copyq-tests testConfigSync`，25 passed，0 failed，退出码 0。

这次提交把桌面同步、测试和规格文档放在一起。桩删除和编译修复发生在任何提交之前，没有可以单独拆出去的重构提交。不要把测试改弱后再推送。

发布目标是 fork `dennischancs/CopyQ` 的分支 `codex/webdav-config-sync`，不推 `upstream`。这不是带安装包的 CopyQ 版本发布：本机没有 `gh`、`cosign` 和 CI 产物，不跑 `utils/github/draft-release.sh`。

## 验收命令

```
source utils/env.sh
export QT_QPA_PLATFORM=offscreen
export COPYQ_TESTS_FILTER=testConfigSync
build/copyq-tests testConfigSync
```

本地绑定被沙箱拦住时，这个命令需要在沙箱外跑。不要用 `python3 -m unittest discover` 代替它。`gate_check.sh` 会误跑那个命令，见 `REVIEW-CHECKLIST.md`。
