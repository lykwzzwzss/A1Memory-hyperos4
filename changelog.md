# Changelog

## v1.1.0 (2026-09-13)

- 修复 HyperOS 后台进程被误判为前台的问题；
- 改用 `topResumedActivity` 识别当前前台游戏；
- 增加 `oom_score_adj` 原值记录和安全恢复；
- 避免覆盖外部修改过的进程优先级；
- 修正 KernelSU / Android 17 下的 SELinux 文件访问策略；
- 默认关闭自动清理；
- 以 16KB page size 对齐方式重新编译 arm64 守护进程；
- 增加 Android 17 / HyperOS 4 安装、配置和验证说明。

## v1.0.0 (2026-08-16)

- 初始版本；
- 支持 HyperOS 4 / Android 17；
- 支持游戏进程保护和可选内存清理。
