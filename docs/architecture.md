# 架构

单个 `ChatGPTCodexUsageMonitor.exe` 用参数区分 launcher/monitor/demo。launcher 先做一次运行进程检查，再通过 Windows PackageManager 动态枚举 MSIX 包及 AppListEntry AUMID，使用 `IApplicationActivationManager` 启动；没有硬编码安装路径。

生命周期完全事件驱动：`SetWinEventHook` 接收窗口创建、销毁、显示、隐藏和位置变化；进程句柄由线程池等待；UI 使用 Windows 消息循环。所有有效顶层窗口消失后只启动一个 5 秒防抖定时器。最小化和暂时隐藏仍算有效窗口，窗口重建会取消退出。跟随模式把连续位置事件合并为 40 ms 一次。

App Server 是每个监视器生命周期内唯一的隐藏子进程。一个阻塞管道读取线程等待 JSONL，不忙等。额度选择层用 `windowDurationMins` 识别 300 分钟的 5 小时窗口与 10,080 分钟的周窗口，优先选取同一个 Codex bucket；`account/rateLimits/updated` 的稀疏通知会按 bucket/window 合并到最近完整快照。常态只有 60 秒额度刷新定时器；能量蓝灯、石化和不可用状态没有动画定时器，红灯只按半周期翻转一次并按需重绘。

HUD 使用 Direct2D 软件 HwndRenderTarget 与 DirectWrite，避免永久 GPU/60 FPS 循环。双额度圆环由深色周额度外圈与亮色 5H 内圈组成；进度条采用同样的深色上条、亮色下条映射。两种额度图形均不随警告相位闪烁，只有胸灯切换相位。圆核、金属水滴护壳、渐变、光晕和裂纹均在 WM_PAINT 中用矢量几何绘制。
