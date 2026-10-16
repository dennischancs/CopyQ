# Gate 验收清单

> 规则在 `SPEC.md` 和 `ARCH.md`。这里只记录核对结果。日期：2026-10-16。

自动化命令：

```
bash /Users/dennischan/.codex/skills/spec-to-acceptance/scripts/gate_check.sh /Users/dennischan/Downloads/GitHub/CopyQ
```

它不编译 C++，也不运行 `build/copyq-tests`。

## 1. 意图

- [x] 桌面行为对应 SPEC R1–R15。上传前 Apply、取消不写盘、坏包不写盘、排除历史，都有测试或在 `configtabbackup.cpp` 里对得上
- [x] 没有做 Karing 的代理、iCloud、局域网同步、自动备份
- [x] 错误码表没有把未知错误当成正常返回。`UNKNOWN` 只在穷尽 switch 之后，文档写明不可达
- [x] 移动端没有被实现成半成品工程
- [ ] GUI 点击、TLS、超时、Windows 重启、真正的 `_Exit` 没有自动化。见 `todo.md`

## 2. 测试

- [x] `testConfigSync` 23 个用例都在桩打开时失败过。21 个是 `NOT_IMPLEMENTED`，另外两个是保存和重启计划被桩跳过
- [x] 沙箱里 `listen` 失败的那次没有当作红灯
- [x] 删桩后 `COPYQ_CONFIG_SYNC_STUB=1 build/copyq-tests testConfigSync`：25 passed，0 failed，退出码 0
- [x] 测试断言的是错误码、文件内容和请求次数，不是私有函数名
- [ ] 测试和实现是同一次会话里的产物。没有第二个人，也没有把测试藏起来。这是降级，不是结构性隔离

## 3. 非功能

- [x] 同步路径没有 SQL
- [x] 没有把密码写进日志或 `Result.detail`。认证测试检查了详情、远程名和错误文案
- [x] 密码保存在用户设置里，并因此进入 zip。这是 SPEC 写明的，不是源码里的字面量口令
- [x] `WebDav` 不依赖界面，`ConfigArchive` 不依赖网络

## 4. 整洁

- [x] 桩函数已从 `configarchive.cpp`、`configsync.cpp`、`webdavclient.cpp` 删除
- [x] 没有新依赖
- [ ] `gate_check.sh` 的 dead-code 警告指向既有 `utils/` 和翻译文件，不是这次的新文件。启发式，不作为失败

## 5. 范围

- [x] 产品代码在 `src/common`、`src/gui`、`src/tests` 和文档
- [x] 没有临时兼容层。曾经的桩是为了红灯，绿灯前已删除

## 6. Gate 豁免

2026-10-16 在 `TASKS.md` 补齐之后再跑一次：`FAIL 42 · WARN 2 · PASS 3 · SKIP 2`，退出码 1。`SPEC.md`、`ARCH.md`、`TASKS.md` 已是 PASS。已知且接受的 FAIL：

| 检查 | 位置 | 为什么豁免 |
|------|------|------------|
| `sql-concat` | `shared/gnome-extension/extension.js`、`translations/copyq_*.ts` | 既有文件。正则把字符串拼接和翻译文本当成 SQL。本任务没有 SQL，也不改这些文件 |
| `test-run` | `python3 -m unittest discover` | 仓库被误认成 Python 项目，收集到 0 个测试。CopyQ 的验收命令是 `build/copyq-tests testConfigSync`，结果在 SPEC 第 5 节 |

SKIP 不等于通过：`test-vs-impl` 因为没有 staged 改动而跳过；`lint` 因为没有 ruff / npm lint 而跳过。C++ 编译警告（`-Wnrvo` 等）不是 Gate 的输入。

## 人工确认

- [x] 上传按钮先 `syncRequested` 再保存 WebDAV 字段再打包
- [x] 下载确认在回调里，取消只写状态
- [x] 删除没有确认框
- [x] 重启计划在 Unix 上不把可执行路径拼进 shell 命令字符串
