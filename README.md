# A1-内存管理 [HyperOS4]

基于原 [OneB1ank/A1Memory](https://github.com/OneB1ank/A1Memory) 的**全新重写版**,面向 HyperOS 4(Android 17)等新平台。

## 定位:只做一件事 —— 游戏保活

场景:打游戏时切出去回 QQ / 微信 / 支付宝,回来后游戏还在。

原理:
1. 游戏切到后台后,守护进程把它主进程的 `oom_score_adj` 锁定为 `0`(前台级),子进程锁定为 `200`,让 LMKD 优先回收其他缓存进程而不是你的游戏;
2. 一次性为游戏设置系统级豁免:`deviceidle` 白名单、`RUN_IN_BACKGROUND` / `RUN_ANY_IN_BACKGROUND` 允许、standby bucket 设为 `active`,降低被 HyperOS 后台管控误杀的概率;
3. 可选:当系统真实内存压力(PSI avg60)超过阈值时,只清理**无关的、缓存的**应用进程(不碰前台、不碰白名单、不碰受保护游戏),给系统腾出余量,避免 LMKD 被迫对游戏动手。

## 为什么不是"负优化"

- **砍掉了与高版本 Android 重合的功能**:不再周期杀缓存进程、不再强制释放内存、不再 hook lmkd、不再休眠应用、不再覆盖系统属性。这些在 Android 12+ 都已是 LMKD / cgroup v2 freezer / 应用待机分桶的原生职责,重复干预只会造成冷启动变多、内存抖动、耗电。
- **默认零干预**:没有游戏在保护窗口内时,守护进程只每隔 5 秒扫一次 `/proc`(开销可忽略),不做任何动作。
- **只碰该碰的**:锁定的是你自己配置的游戏的进程;清理只在真实内存压力下触发,且最多 8 个/次、60 秒冷却。
- **可审计**:核心是新写的 C 源码(`src/a1guard.c`,约 500 行),无闭源二进制、无 inline hook、无属性覆写。

## 兼容性

- 平台:arm64 新机(Android 15 / 16 / 17,HyperOS 4)
- 16KB 内存页:二进制以 `-Wl,-z,max-page-size=16384` 编译,LOAD 段对齐 0x4000,可在 16KB 页设备上加载(同时兼容 4KB 页设备)
- 框架:Magisk / KernelSU / APatch 通用模块格式
  - KernelSU:`ksud module install A1Memory-hyperos4-<date>.zip` 或 KernelSU 管理器直接刷入
  - 已内置最小 `sepolicy.rule`(su 域读写 `oom_score_adj`、向应用进程发信号),如遇 avc 拒绝可用 `dmesg | grep avc` 排查

## 安装

1. 把游戏包名加入 `/data/adb/modules/a1memory_hyperos4/config/game.conf`(一行一个,`#` 注释);
2. Magisk 中刷入模块 zip,重启;
3. 日志在 `/data/local/tmp/a1guard.log`,用于确认保护是否生效。

> 提示:game.conf 修改后**无需重启**,守护进程每轮都会检查配置变更并热重载。

## 配置说明

`/data/adb/modules/a1memory_hyperos4/config/guard.conf`:

| 键 | 默认 | 说明 |
| --- | --- | --- |
| enable | 1 | 总开关 |
| poll_ms | 2000 | 游戏在后台时的轮询间隔(ms) |
| idle_poll_ms | 5000 | 无游戏时的检查间隔(ms) |
| protect_minutes | 30 | 切出游戏后的保护时长(分钟) |
| pin_adj_main | 0 | 主进程锁定 oom_score_adj |
| pin_adj_child | 200 | 子进程锁定 oom_score_adj |
| cleanup | 1 | 压力大时清理无关缓存应用 |
| psi_threshold | 40.0 | PSI avg60 压力阈值(%) |
| cleanup_interval_ms | 15000 | 压力检查间隔 |
| cleanup_cooldown_ms | 60000 | 清理冷却 |
| cleanup_max_kill | 8 | 单次最多清理进程数 |
| log | 1 | 日志开关 |

`whitelist.conf`:白名单应用,其缓存进程永不被清理。

## 从源码构建

```bash
./scripts/build.sh            # 自动下载 NDK r27c, 编译并校验 16KB 对齐
./scripts/build.sh package    # 额外打包模块 zip 到 build/
```

环境变量:`NDK_VERSION`(默认 r27c)、`NDK_DIR`、`API`(默认 35)。

## 卸载

直接在 Magisk/KSU 中卸载即可;卸载脚本会停止守护进程、删除日志,并尽力撤销对游戏包的系统豁免。

## 验证清单(建议装前/装后对比)

- 二进制可运行:`/data/adb/modules/a1memory_hyperos4/bin/a1guard` 能启动且日志出现 `config loaded`
- 保护生效:游戏中切到微信再切回,游戏进程仍在(`dumpsys activity lru` 或 `ps -A | grep <游戏>`)
- 无负优化:`/proc/pressure/memory` 压力无明显升高,待机耗电无明显增加,冷启动次数不增多
