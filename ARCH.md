# ARCH — 架构约束锚定

> **文档类型：权威事实源**
> 技术约束以本文件为准。行为规则以 `SPEC.md` 为准，不要在这里重写。

## 1. 技术栈

| 层 | 选型 | 版本 | 理由 |
|---|------|------|------|
| 语言 | C++ | 跟随仓库，Qt 6 | CopyQ 本体 |
| 界面 | Qt Widgets | 本机验证用 Qt 6.12.0 | 偏好设置已是 Widgets |
| 网络 | `QNetworkAccessManager` | Qt 自带 | 不新增 HTTP 库 |
| 压缩 | 自写 ZIP STORE | 无第三方 | SPEC B6 |
| JSON | `QJsonDocument` | Qt 自带 | 清单很小 |
| 测试 | Qt Test，`build/copyq-tests` | 只跑 `testConfigSync` | 项目既有测试入口 |
| 构建 | CMake + Ninja | 仓库现成 | `utils/env.sh` |

**新增依赖**：本任务不允许。ZIP、HTTP、JSON 都用 Qt 和标准库。

本机配置命令（缺包，不是产品依赖）：

```
cmake -B build -DWITH_TESTS=ON -DPEDANTIC=ON \
  -DWITH_QCA_ENCRYPTION=OFF -DWITH_KEYCHAIN=OFF \
  -DWITH_NATIVE_NOTIFICATIONS=OFF -DWITH_AUDIO=OFF .
```

跑测试前把 `QT_QPA_PLATFORM` 设为 `offscreen`。`utils/env.sh` 会把它设成 `xcb`，在这台 Mac 上不能留着。测试要绑定 127.0.0.1，沙箱会拒绝。

## 2. 目录

```
src/common/configsyncerror.*   错误码和文案
src/common/configarchive.*     打包、校验、恢复
src/common/webdavclient.*      HTTP/WebDAV
src/common/configsync.*        连接保存、上传、下载、重启计划
src/gui/configtabbackup.*      Backup 页
src/gui/configurationmanager.* 把该页放进偏好设置
src/tests/configsynctests.*    验收测试
docs/schemas/copyq-config-manifest.schema.json
```

依赖方向：界面 → `ConfigSync` → `WebDav` 与 `ConfigArchive`。`ConfigArchive` 不发网络请求。`WebDav` 不写设置目录。测试可以调用这三层，生产代码不能依赖测试。

`ConfigTabBackup` 上传时先发 `syncRequested`。`ConfigurationManager` 收到后走现有 `apply()`。这是为了 SPEC R2，不是让同步模块去摸每个设置项。

## 3. 命名

| 对象 | 约定 | 正例 |
|------|------|------|
| 错误码 | `SCREAMING_SNAKE` 字符串 | `ARCHIVE_INVALID` |
| 类型 | `ConfigSync` / `ConfigArchive` / `WebDav` | 不要再起 `BackupManager` |
| 设置组 | `WebDAV` | 键只有 `url` `user` `password` |
| 清单 | `copyq-config.json` | 不改名 |
| 测试组 | `testConfigSync` | `IMPL_TEST_GROUP(ConfigSync)` |

## 4. 变化原因

| A | B | A 可以依赖 B？ | 原因是否相同 |
|---|---|---------------|----------------|
| Backup 页 | 打包格式 | 否，只通过 `ConfigSync` | 否。界面文案和 zip 布局会分开变 |
| WebDAV | zip | 否 | 否。服务器和包格式分开变 |
| 上限常量 | 调用点 | 调用点读 `ConfigArchive` 常量 | 是。上限只许改一处 |
| 移动端 | 以上模块 | 现在不存在这条边 | 移动端是另一个产品形态，见 `todo.md` |

## 5. 禁止

- 不要恢复 `COPYQ_CONFIG_SYNC_STUB` 或任何让生产代码改走未实现的开关。
- 不要在日志、`Result.detail`、对话框里放密码。
- 不要跟随重定向，不要加「忽略证书」开关。
- 不要为了过 Gate 去改 `shared/gnome-extension` 或 `translations/*.ts`。那些 sql-concat 命中是既有误报。
- 不要新建 Android 或 iOS 工程来制造「已经移植」的外观。
