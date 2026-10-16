# CopyQ 配置同步 — 产品说明

> 验收句子的唯一权威源是 [SPEC.md](SPEC.md) 第 1 节。本文件不复述「假如…那么…」。冲突时以 SPEC 为准。
> 进度以 [TASKS.md](TASKS.md) 为准。没做完的移动端写在 [todo.md](todo.md)。

## 要解决的事

用户已经在一台电脑上配好 CopyQ。换一台电脑时，希望把参数和配置上传到 WebDAV，在另一台下载同一个 zip 即可还原。剪贴板历史不在这个包里。

## 用户怎么用

偏好设置里有一页 **Backup**。上面是地址、账号、密码（密码遮住）。按钮是上传、刷新、重置、下载、删除。

- 上传前，对话框里还没 Apply 的其他设置会先按 Apply 写盘，再打进 zip。
- 下载会先问要不要替换本机配置。取消则本机不动。确认后替换受管配置并尝试重启。
- 删除不二次确认。
- 重置只清空地址、账号、密码和当前列表。
- 错误用对话框。取消恢复只改状态文字。

远程列表按文件名从新到旧。新上传的名字类似 `copyq-config-20261016T153045123Z.zip`。

## 范围

做：桌面端 WebDAV 配置 zip 的上传、列表、下载恢复、删除、重置。

不做：Karing 的代理模式、iCloud、局域网同步、自动备份；剪贴板历史同步；关闭证书校验；跟随重定向；Android 和 iOS 客户端。原因在 SPEC 的边界表和 `todo.md`。

zip 里会带上保存在设置文件中的 WebDAV 密码。这个包要当秘密文件保管。

## 验收标准

标准正文是 SPEC 的 R1–R15 和 B1–B8。本交付的通过条件：

1. R1–R15 每条都能在 `src/tests/configsynctests.cpp` 里找到对应用例（对照表在 SPEC 第 5 节）。
2. 这些用例在实现被桩住时失败，原因是功能未实现，不是编译失败。
3. 桩删除后，同一批用例在 `COPYQ_CONFIG_SYNC_STUB=1` 下仍然通过。2026-10-16 发布前复跑 `build/copyq-tests testConfigSync`：25 passed，0 failed，退出码 0。
4. 移动端没有被标成完成。

## 测试用例

步骤是人工可读的操作。自动测试用同名函数，不启动完整 CopyQ 界面。

| 用例 | 步骤 | 期望 | 自动测试 | 规则 |
|------|------|------|----------|------|
| TC1 | 设置目录里放好 ini，上传，再下载到空目录并确认 | ini 内容一致，且标记需要重启 | `roundTripRestoresManagedFiles` `uploadsListsDownloadsAndDeletesInDescendingOrder` | R1 R12 |
| TC2 | 目录里同时有历史、状态、隐藏文件、锁、日志、zip、指向目录外的符号链接 | 打包和恢复都不带走、不删掉这些 | `excludesHistoryLocksLogsZipSymlinksStateAndItems` `replacesManagedFilesOnly` | R12 |
| TC3 | 只有被排除的文件，没有普通配置 | 拒绝上传，本机不产生空包副作用 | `emptyDirectoryIsRejected` | SPEC `ARCHIVE_EMPTY` |
| TC4 | 喂入路径穿越、坏字节、空清单、多余字段、超大声明、错误 CRC、垃圾下载 | 目标目录维持原样 | `rejectsZipSlip` `rejectsInvalidBytes` `emptyManifestDoesNotWipe` `rejectsManifestOutsideContract` `oversizedDeclarationDoesNotWipe` `checksumMismatchDoesNotWipe` `garbageDownloadDoesNotWipe` | R10 |
| TC5 | 恢复时某个父路径已经是文件 | 失败，先前写出的内容回滚 | `rollsBackWhenParentIsAFile` | R11 |
| TC6 | 密码空白但 URL 也不合法；ftp；URL 内嵌账号密码；合法连接 | 空白报缺凭据；非法协议和内嵌凭据报 URL 错误；合法连接在校验阶段请求数为 0 | `credentialsAndUrlDoNotConnect` | R3 R4 |
| TC7 | 上传两个时间戳名字并夹一个非 zip | 列表只有 zip，且新名字排在旧名字前；删除后该名字消失 | `uploadsListsDownloadsAndDeletesInDescendingOrder` | R7 R8 |
| TC8 | 用错误密码列目录 | 认证失败，返回文本里没有真密码也没有这次输入的密码 | `authFailureDoesNotLeakSecret` | R6 |
| TC9 | 服务器对列表回 302 | 拒绝重定向，只打了 1 次请求 | `redirectIsNotFollowed` | R5 |
| TC10 | 下载后确认函数返回否 | 本地 ini 仍是原内容，不需要重启 | `cancelLeavesLocalFiles` | R9 |
| TC11 | 确认恢复 | 文件已替换，`needsRestart` 为真，测试进程没有退出 | `confirmRestoreNeedsRestartWithoutExiting` | R12 |
| TC12 | 保存连接再重置 | 三个字段先读得回来，重置后组被删掉 | `connectionSaveAndReset` | R14 |
| TC13 | 远程名含 `..` 或不是 zip | 不增加请求计数，本地文件不变 | `unsafeRemoteNameDoesNotConnect` | R13 |
| TC14 | 端口已关闭 | 网络失败 | `closedServerIsNetworkFailed` | SPEC `NETWORK_FAILED` |
| TC15 | 查看重启计划 | Unix 上是 `/bin/sh`，延迟在参数里，可执行文件不拼进 shell 字符串 | `restartPlanDelaysRelaunch` | SPEC A7 |
| TC16 | 打开 `COPYQ_CONFIG_SYNC_STUB` 再打包恢复 | 仍然成功 | `environmentSwitchCannotDisableSync` | R15 |

没有自动用例、因此不能签收的部分：30 秒超时、TLS 失败、Backup 页真实点击、Windows 重启命令、`startDelayedRestart` 真正退出进程。这些留在 `todo.md`。

## 一致性

恢复要么整包成功并要求重启，要么本机配置保持恢复前。不接受「写了一半还继续用」。10 分钟内进程没重启是允许的，但必须有文字告诉用户。细节在 SPEC 第 1 节的一致性表。
