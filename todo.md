# TODO

状态说明：勾选表示这件事已经有证据。未勾选就是没做完。手机端不在这张桌面清单里标完成，见 `mobile/todo.md`。

## 桌面 WebDAV 配置同步

- [x] Backup 页：地址、账号、密码、重置、列表、上传、确认后下载、无确认删除
- [x] STORE zip 与 `copyq-config.json` 清单
- [x] 排除历史、状态、隐藏文件、锁、日志、zip、符号链接
- [x] 坏包不改本机，写入失败回滚
- [x] 不跟随重定向，错误详情不含密码
- [x] `testConfigSync` 绿灯（2026-10-16，25 passed，0 failed）
- [ ] Backup 页没有界面自动化。按钮、对话框、上传前 Apply 只做了代码检视
- [ ] 30 秒超时没有测试
- [ ] TLS 失败没有测试
- [ ] `startDelayedRestart` 会 `_Exit`，测试没有执行它
- [ ] Windows `cmd.exe` 重启字符串没有在 Windows 上运行
- [ ] zip 含设置文件里的 WebDAV 密码。产品已接受，但没有额外的加密层
- [ ] 两个进程同时恢复同一设置目录没有定义

## Android 和 iOS

手机端计划在 `mobile/prd.md`，规则在 `mobile/SPEC.md`，还没做的分级在 `mobile/todo.md`。

本分支仍然没有 Android 工程，没有 iOS 工程，没有 APK，也没有 IPA。计划写完不等于移植完成。不能移植成电脑全量功能的原因留在 `mobile/todo.md`，不要在这里再写一张表。
