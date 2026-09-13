# A1-内存管理 [HyperOS4]

一个面向 HyperOS 4 / Android 15、16、17 的轻量级游戏进程保护模块。

## 1.1.0

这一版来自真实设备调试和多应用后台测试，重点修复了旧版在 HyperOS 上的几个问题：

- 使用 `topResumedActivity` 判断真正的前台应用，避免把 HyperOS 后台进程误判成前台；
- 游戏切入前台时保护主进程和子进程，切到后台后恢复系统调度，避免永久锁死进程优先级；
- 记录被调整进程的原始 `oom_score_adj`，退出保护窗口后安全恢复；
- 增加进程状态跟踪，外部程序修改过优先级时不会强行覆盖；
- 针对 KernelSU 的 `ksu` 域和 Android 17 `untrusted_app_30` 文件类型补充最小策略；
- 以 16KB page size 对齐方式编译 arm64 二进制；
- 默认关闭自动清理，降低误杀、卡顿和耗电风险。

## 工作方式

模块守护进程按配置扫描指定游戏。当游戏位于前台时，为主进程设置 `oom_score_adj=0`，为相关子进程设置 `oom_score_adj=200`；当游戏离开前台时，恢复系统调度。模块不会持续杀后台应用，也不会修改系统属性。

当前版本只在真实 PSI 内存压力达到阈值时才支持可选清理；默认 `cleanup=0`，建议先保持关闭并观察稳定性。

## 兼容性

- arm64；
- Android 15 / 16 / 17；
- HyperOS 4；
- Magisk、KernelSU、APatch 模块格式；
- 兼容 4KB 和 16KB 内存页设备。

不同厂商的 SELinux 类型可能不同。仓库内的 `sepolicy.rule` 针对 Android 17 / KernelSU 场景编写，换设备后应先检查 AVC 日志和目标文件安全上下文。

## 安装

1. 从 [Releases](https://github.com/lykwzzwzss/A1Memory-hyperos4/releases/latest) 下载 `A1Memory-hyperos4-1.1.0.zip`；
2. 在 KernelSU、Magisk 或 APatch 中刷入；
3. 重启设备；
4. 修改 `config/game.conf` 后无需重新编译，守护进程会自动热加载。

## 配置

模块目录：`/data/adb/modules/a1memory_hyperos4/`

`config/game.conf`：每行一个需要保护的游戏包名。

`config/whitelist.conf`：不会参与可选清理的应用包名。

`config/guard.conf` 常用选项：

| 选项 | 默认值 | 作用 |
| --- | ---: | --- |
| `enable` | `1` | 守护进程总开关 |
| `poll_ms` | `1000` | 有游戏时的检查间隔 |
| `idle_poll_ms` | `10000` | 无游戏时的检查间隔 |
| `protect_minutes` | `5` | 最近一次前台游戏的保护窗口 |
| `pin_adj_main` | `0` | 主进程目标 `oom_score_adj` |
| `pin_adj_child` | `200` | 子进程目标 `oom_score_adj` |
| `cleanup` | `0` | 是否启用 PSI 压力清理 |
| `log` | `1` | 是否记录日志 |

日志路径：`/data/local/tmp/a1guard.log`

## 验证

可以用以下方式确认模块已启动：

```sh
su -c 'pidof a1guard'
su -c 'tail -n 30 /data/local/tmp/a1guard.log'
```

建议先启动游戏，再打开微信、音乐、设置等应用，让游戏处于后台，确认游戏主进程和子进程仍存在；随后再切回游戏，观察是否正常恢复前台优先级。

## 从源码构建

```sh
./scripts/build.sh
./scripts/build.sh package
```

默认使用 Android NDK r27c，构建目标为 arm64 / API 35，并检查 ELF LOAD 段的 16KB 对齐。

## 许可证

本项目采用 GPL-3.0，详见 [`LICENSE`](./LICENSE)。
