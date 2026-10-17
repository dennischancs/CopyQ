# SPEC — 意图与验收标准

> **文档类型：权威事实源**
> 冲突时以本文件为准。变更必须走 Gate。
> `prd.md`、`dev-plan.md`、`TASKS.md`、`todo.md` 只引用规则编号，不另写一套「假如…那么…」。

## 0. 一句话说明

让一台 CopyQ 把当前参数和配置打成 zip 放到 WebDAV，另一台 CopyQ 下载同一个 zip 后恢复成同一套配置，且剪贴板历史保持不动。

**当前状态**：已冻结，2026-10-16。桌面同步已实现。Android / iOS 不在本交付内。

## 1. 意图层

界面参考 Karing 的 WebDAV 备份页（地址、账号、密码、重置、远程 zip 列表、上传、下载确认后恢复、删除不二次确认、错误用对话框）。CopyQ 把这些控件放在偏好设置的 Backup 页，不打开第二层页面。不复制 Karing 的代理连接模式、iCloud、局域网同步和自动备份。

### 规则

| # | 规则 | 来源 |
|---|------|------|
| R1 | 假如我在 Backup 页填写 WebDAV 地址、账号和密码并上传，那么服务器上出现一个配置 zip，另一台设备用同一地址和账号下载并确认后，应当得到同一套配置文件。 | 用户：配置可上传、下载后还原 |
| R2 | 假如上传时偏好设置对话框里还有没按 Apply 的修改，那么这些修改要先写入配置，再进入 zip。刷新、下载、删除、重置只保存地址、账号和密码，不顺带提交其他页的修改。 | 与现有 Apply 行为一致 |
| R3 | 假如地址、账号、密码有任何一项为空或只有空白，那么不连接服务器，并提示需要填写这三项。空白优先于地址格式错误。 | Karing 三项必填；空凭据优先 |
| R4 | 假如地址不是 http 或 https，地址不合法，或地址里夹带了账号密码，那么拒绝，且不连接。 | 只允许 http(s)，禁止 userinfo |
| R5 | 假如服务器返回重定向，那么不跟随，并说明拒绝了重定向。 | 不跟随重定向 |
| R6 | 假如服务器拒绝账号或密码，那么说明认证失败，界面文字和错误详情里都不出现密码。 | 不记录秘密 |
| R7 | 假如列出远程文件，那么只显示安全的 zip 名，并按名字从新到旧排列。 | Karing 降序 |
| R8 | 假如我删除列表中的一个备份，那么不要求再次确认，删除后列表不再包含它。 | Karing 删除无确认 |
| R9 | 假如我下载后选择取消，那么本机配置文件一个字节都不改。 | 取消不落盘 |
| R10 | 假如 zip 不是 CopyQ 配置包、清单为空、清单多了字段、校验和不符、或路径试图逃出配置目录，那么本机已有文件保持原样。 | 先整包校验，失败不写 |
| R11 | 假如恢复写到一半失败，那么已经改过的受管文件回到恢复前的内容。 | 失败回滚 |
| R12 | 假如恢复成功，那么受管配置被替换，被排除的文件（历史、状态、锁、日志、隐藏文件、符号链接）保持不动，并需要重启才算应用到正在运行的程序。重启安排失败时，明确告诉用户手动重启。 | 配置还原，历史不还原 |
| R13 | 假如远程名字不是安全的 zip 文件名，那么不连接服务器。 | 防路径和奇怪名字 |
| R14 | 假如我重置连接，那么只清空地址、账号和密码以及当前列表。 | Karing 重置这三项 |
| R15 | 假如设置环境变量试图关掉同步实现，那么同步仍然按上面的规则工作。 | 防止用开关把实现变回未完成 |

### 边界

| # | 不做 | 理由 |
|---|---------|------|
| B1 | 不复制 Karing 的代理连接模式、iCloud、局域网同步、配置自动备份 | 用户只要 WebDAV 配置包 |
| B2 | 不同步剪贴板历史、标签内容、同步目录里的条目数据 | 历史不是「参数和配置」；体积和隐私都不适合打进配置包 |
| B3 | 不跟随符号链接，不打包隐藏文件、`*.lock`、`*.log`、`*.zip`、清单文件自身 | 避免把运行时垃圾和链接目标打进去 |
| B4 | 不打包设置目录以外的 GnuPG 钥匙圈 | 钥匙不在设置目录里 |
| B5 | 不关闭 TLS 校验，不跟随 HTTP 重定向 | 避免把口令送到另一个主机 |
| B6 | 不新增压缩库；zip 只用 STORE | 仓库不增加依赖 |
| B7 | 本交付不实现 Android 或 iOS 工程 | 桌面配置同步这一交付仍不包含移动工程。手机端见 `mobile/SPEC.md`，不改写本文件的 R1–R15 |
| B8 | 删除远程备份不做确认 | 有意对齐 Karing，不是遗漏 |

### 一致性

| 规则 | 出错时用户看到什么 | 不一致持续 10 分钟会怎样 | 可接受 |
|------|------------------|----------------------|--------|
| R1 R12 | 成功则提示正在重启；失败则对话框说明原因 | 磁盘已是新配置但进程仍用旧配置，直到重启。用户被明确告知 | 可以。不能静默 |
| R9 R10 R11 | 取消或坏包：本机文件不变 | 没有半份配置。最多是用户以为恢复了但实际没有，界面有错误或取消文案 | 可以 |
| R3 R4 R13 | 不联网 | 没有远程副作用 | 可以 |
| R6 | 认证失败且不含密码 | 用户可改密码重试。密码不进详情 | 可以 |
| R2 | 上传包含刚改未 Apply 的项 | 若上传失败，对话框里的修改可能已经 Apply 到本机 | 可以。上传前先 Apply，和按 Apply 相同 |

## 2. 数据契约

清单文件名固定为 `copyq-config.json`。schema：`docs/schemas/copyq-config-manifest.schema.json`。

```
copyq-config.json
  format: string  只能是 copyq-config
  format_version: number  只能是 1
  files: string[]  至少 1 个，不可重复，且必须和 zip 里除清单外的文件一一对应
```

不允许额外字段。空的 `files` 是 `ARCHIVE_INVALID`，不是 `ARCHIVE_EMPTY`。

每个 `files` 条目是相对设置目录的路径：不用反斜杠和盘符冒号，不以 `/` 开头，不含空段、`.`、`..`，深度不超过 8，整段不超过 240，单个路径分量不超过 200，不含控制字符。文件名不能以 `.` 开头，不能是清单名，不能以 `.lock`、`.log`、`.zip` 结尾。

zip 是 STORE（方法 0），UTF-8 文件名，带 CRC32。上限只定义在 `ConfigArchive::maxFiles`（500）、`maxBytes`（32 MiB）、`maxFileBytes`（8 MiB）。

上传文件名：`copyq-config-` + UTC `yyyyMMddTHHmmsszzz` + `Z.zip`。

WebDAV 连接保存在设置里的 `WebDAV` 组：`url`、`user`、`password`，保存前去掉首尾空白。这三个值会随设置目录一起被打包。zip 因此含有 WebDAV 密码，必须当秘密文件保管。日志和错误详情不得出现密码。

远程安全名：`^[A-Za-z0-9][A-Za-z0-9._-]{0,180}\.zip$`。

### 校验

| # | 字段 | 规则 | 违反时 |
|---|------|------|--------|
| V1 | url/user/password | 去空白后都非空 | `CREDENTIALS_MISSING` |
| V2 | url | StrictMode，有 host，scheme 为 http 或 https，无 userinfo | `URL_INVALID` |
| V3 | 远程名 | 匹配安全名 | `NAME_INVALID` |
| V4 | 清单 | 仅三个键，format 与版本正确，files 与条目一致 | `ARCHIVE_INVALID` |
| V5 | 体积 | 文件数、单文件、总字节不超过上面的三个常量 | `ARCHIVE_TOO_LARGE` |
| V6 | 压缩方法 | 必须是 0，CRC 必须相符 | `ARCHIVE_INVALID` |

本环境没有 `ajv` / `jsonschema`。schema 与 `configarchive.cpp` 的键集合是人工核对的：只允许 `format`、`format_version`、`files`。

## 3. 接口契约

这不是 HTTP API。错误码是本机枚举，没有「500 未知错误」给调用方。`errorCode()` 在 switch 之后有一条不可达的 `UNKNOWN` 返回，只为编译器而存在，不进入契约。

| # | 动作 | 入参 | 成功时 | 错误码 |
|---|------|------|--------|--------|
| A1 | 校验连接 | url, user, password | 规范化后的目录 URL（补上末尾 `/`，去掉 fragment 和 userinfo） | `CREDENTIALS_MISSING` `URL_INVALID` |
| A2 | 列目录 | 连接 | 安全 zip 名，字典序降序 | 连接错误，以及 `AUTH_FAILED` `NOT_FOUND` `CONFLICT` `REDIRECT_REJECTED` `NETWORK_FAILED` `TIMEOUT` `TLS_FAILED` `LIST_FAILED` `ARCHIVE_TOO_LARGE` |
| A3 | 上传设置 | 连接 + 设置/历史/状态目录 | `remoteName` 为新 zip 名 | 连接错误，`ARCHIVE_EMPTY` `ARCHIVE_TOO_LARGE` `IO_FAILED` `UPLOAD_FAILED` 及传输错误 |
| A4 | 下载并恢复 | 连接、远程名、目录、确认函数 | `needsRestart=true`，受管文件已替换 | 连接错误，`NAME_INVALID`，传输错误，`CANCELLED`，`ARCHIVE_*` `RESTORE_FAILED` `IO_FAILED` |
| A5 | 删除 | 连接、远程名 | 远程文件消失 | 连接错误，`NAME_INVALID` `DELETE_FAILED` 及传输错误 |
| A6 | 保存/读取/重置连接 | QSettings | 仅三个字段；重置删除整个 WebDAV 组 | 无网络错误 |
| A7 | 计划重启 | 可执行文件路径 | Unix：`/bin/sh -c 'sleep 1; exec "$0" "$@"' <exe>`，且不把 exe 写进 shell 字符串。Windows：`cmd.exe /d /c` 用 ping 延迟后再 start | 计划本身不失败 |
| A8 | 执行重启 | A7 的计划 | 启动后 `std::_Exit`，避免 QSettings 在退出时写回旧 ini | 启动失败则调用方报告 `RESTART_FAILED` |

HTTP 映射：401/403 认证失败；404 不存在；409/412/423 冲突；300–399 拒绝重定向；其他非 2xx 用该操作自己的失败码（列表/上传/下载/删除）。超时 30 秒。响应体超过对应上限则为 `ARCHIVE_TOO_LARGE`。

Basic 认证。不提供关闭证书校验的开关。

### 错误码

| 错误码 | 触发 | 用户应当 |
|-------|------|---------|
| `CREDENTIALS_MISSING` | 三项有空 | 补全，不要重试网络 |
| `URL_INVALID` | scheme、host、userinfo、非法 URL | 改地址 |
| `NAME_INVALID` | 远程名不安全 | 不要请求该名字 |
| `AUTH_FAILED` | HTTP 401/403 | 改账号或密码。详情不得含密码 |
| `NOT_FOUND` | HTTP 404 | 刷新列表 |
| `CONFLICT` | HTTP 409/412/423 | 刷新后重试 |
| `REDIRECT_REJECTED` | HTTP 3xx | 改成最终地址，不要指望自动跳转 |
| `NETWORK_FAILED` | 没有 HTTP 状态且不是 TLS/超时 | 检查地址和网络 |
| `TIMEOUT` | 30 秒无完成 | 可重试 |
| `TLS_FAILED` | TLS 握手失败 | 不要提供忽略证书的按钮 |
| `LIST_FAILED` | 列表响应不是可用的 PROPFIND，或其他列表 HTTP 失败 | 看服务器 |
| `UPLOAD_FAILED` | PUT 失败 | 可重试 |
| `DOWNLOAD_FAILED` | GET 失败 | 可重试。本机配置不变 |
| `DELETE_FAILED` | DELETE 失败 | 列表保持原样 |
| `ARCHIVE_INVALID` | 坏 zip、坏清单、空清单、额外字段、路径不安全、压缩方法不对、CRC 不对 | 本机不变 |
| `ARCHIVE_TOO_LARGE` | 超过三个上限之一，或响应体超限 | 不恢复 |
| `ARCHIVE_EMPTY` | 设置目录里没有可打包的普通文件 | 不上传空包 |
| `RESTORE_FAILED` | 校验通过但写入/删除受管文件失败，且已回滚 | 本机应与恢复前一致 |
| `IO_FAILED` | 读写临时文件或设置目录失败 | 不继续 |
| `CANCELLED` | 确认函数返回假 | 只显示状态，不弹错误框 |
| `RESTART_FAILED` | 恢复已落盘，但延迟重启进程没启动 | 手动重启 |
| `NOT_IMPLEMENTED` | 仅保留给未完成路径。生产代码不得再靠环境变量返回它 | 视为缺陷 |

## 4. 不变式

| # | 不变式 | 断言位置 |
|---|-------|---------|
| I1 | 坏包、空清单、校验失败、取消，都不改目标目录里已有文件 | `emptyManifestDoesNotWipe` `checksumMismatchDoesNotWipe` `cancelLeavesLocalFiles` `garbageDownloadDoesNotWipe` `rejectsZipSlip` |
| I2 | 恢复只替换受管文件，排除前缀、隐藏文件、lock、log、zip、符号链接保持原样 | `excludesHistoryLocksLogsZipSymlinksStateAndItems` `replacesManagedFilesOnly` |
| I3 | 父路径是文件导致写失败时，已写入的部分回滚 | `rollsBackWhenParentIsAFile` |
| I4 | 空凭据不会因为 URL 也非法而改报 URL 错误，且请求计数为 0 | `credentialsAndUrlDoNotConnect` |
| I5 | 重定向只产生 1 次请求 | `redirectIsNotFollowed` |
| I6 | 认证失败的 detail、remoteName、错误文案都不含密码 | `authFailureDoesNotLeakSecret` |
| I7 | `COPYQ_CONFIG_SYNC_STUB` 不能再让打包或恢复变成未实现 | `environmentSwitchCannotDisableSync` |
| I8 | 三个上限只存在于 `ConfigArchive` 的三个常量 | 人工核对，测试用超大声明触发拒绝 |

### 边界情况

| # | 输入 | 期望 |
|---|------|------|
| E1 | 设置目录没有可打包文件 | `ARCHIVE_EMPTY` |
| E2 | 清单 `files` 为空 | `ARCHIVE_INVALID`，不删除本地文件 |
| E3 | 路径含 `..` 或绝对路径 | `ARCHIVE_INVALID`，不写到目录外 |
| E4 | 并发 | 本交付不承诺两个 CopyQ 同时恢复同一目录。一次恢复在写之前把受管文件读入快照 |
| E5 | 时区 | 上传名用 UTC。列表排序是文件名降序，不是解析时区 |

## 5. 测试基准

测试文件：`src/tests/configsynctests.cpp`。不启动 CopyQ 服务器，不弹出对话框，不调用 `startDelayedRestart`（那个函数会 `_Exit`）。

| 测试 | 规则 |
|------|------|
| `roundTripRestoresManagedFiles` | R1 R12 |
| `excludesHistoryLocksLogsZipSymlinksStateAndItems` | R12 B2 B3 |
| `emptyDirectoryIsRejected` | `ARCHIVE_EMPTY` |
| `rejectsZipSlip` | R10 |
| `rejectsInvalidBytes` | R10 |
| `emptyManifestDoesNotWipe` | R10 |
| `rejectsManifestOutsideContract` | R10 |
| `oversizedDeclarationDoesNotWipe` | R10 |
| `checksumMismatchDoesNotWipe` | R10 |
| `rollsBackWhenParentIsAFile` | R11 |
| `replacesManagedFilesOnly` | R12 |
| `credentialsAndUrlDoNotConnect` | R3 R4 |
| `uploadsListsDownloadsAndDeletesInDescendingOrder` | R1 R7 R8 R12 |
| `authFailureDoesNotLeakSecret` | R6 |
| `redirectIsNotFollowed` | R5 |
| `cancelLeavesLocalFiles` | R9 |
| `confirmRestoreNeedsRestartWithoutExiting` | R12 |
| `garbageDownloadDoesNotWipe` | R10 |
| `connectionSaveAndReset` | R14 |
| `unsafeRemoteNameDoesNotConnect` | R13 |
| `closedServerIsNetworkFailed` | `NETWORK_FAILED` |
| `restartPlanDelaysRelaunch` | A7 |
| `environmentSwitchCannotDisableSync` | R15 |

### 红-绿记录

实现先于测试存在。红灯不是「还没有函数」，而是三个实现文件里的 `COPYQ_CONFIG_SYNC_STUB` 桩：环境变量一旦设置，入口直接返回默认结果（错误码即 `NOT_IMPLEMENTED`）。这是本仓库里能让「功能未实现」失败、而不是编译失败的办法。桩已经删掉，生产路径不再读取该变量。

编译期修过的问题（这些红不算验收红灯）：`ResultListedGuard` 不是类型；`QFileInfo` 的方法是 `isSymLink`；`tests.cpp` 漏了头文件；`configurationmanager.h` 漏了 `m_tabBackup`。

沙箱里的第一次运行也不算红灯：`QTcpServer` 绑定 127.0.0.1 得到 `Operation not permitted`，`dav.start()` 失败。下面的记录是允许本地绑定之后的运行。

**红（桩还在，`COPYQ_CONFIG_SYNC_STUB=1`）**

`testConfigSync` 的 23 个用例全部失败。其中 21 个打印 `NOT_IMPLEMENTED`，实际错误码 1。另外两个是桩的直接后果，不是编译错误：

- `connectionSaveAndReset`：保存被桩跳过，读回的 url 是空字符串。
- `restartPlanDelaysRelaunch`：`planDelayedRestart` 返回空计划，`exitProcessWithoutFlush` 为假。

同一进程里，过滤器仍会进入其他测试组的 `initTestCase`。当时没有构建 `build/copyq`，所以 Core 等 7 组初始化失败。`Totals: 2 passed, 30 failed, 1 skipped`，退出码 30。ConfigSync 自身没有一例是因为编译或绑定失败。

**绿（桩已删除，变量仍然设为 1）**

```
QT_QPA_PLATFORM=offscreen COPYQ_CONFIG_SYNC_STUB=1 build/copyq-tests testConfigSync
Totals: 25 passed, 0 failed, 0 skipped, 0 blacklisted, 41ms
EXIT:0
```

发布前同一命令再跑一次，仍是 25 passed，0 failed，0 skipped，40ms，退出码 0。`environmentSwitchCannotDisableSync` 仍然通过。

25 = `initTestCase` + 23 个用例 + `cleanupTestCase`。日志里 23 个用例逐条 PASS。`environmentSwitchCannotDisableSync` 在变量打开时仍然通过。

### 没被测试覆盖、因此不能声称已验证

- 30 秒超时和 TLS 失败没有用例。
- `startDelayedRestart` 的 `_Exit` 没有执行。
- Backup 页的对话框、按钮和「上传前先 Apply」没有 GUI 自动化。
- Windows 的 `cmd.exe` 重启命令没有在 Windows 上跑过。
- 本机 CMake 关掉了 QCA、Keychain、原生通知和音频，因为对应开发包没装。同步代码不依赖它们，但这次绿不是完整 CopyQ 配置的绿。

## 6. 个人项目的降级

没有第二个人独立写测试。测试和实现都在同一次会话里。机械证据只有：红灯日志、绿灯日志，以及 `gate_check.sh`。Gate 不编译 C++，也不跑 `copyq-tests`。见 `REVIEW-CHECKLIST.md`。
ests`。见 `REVIEW-CHECKLIST.md`。
