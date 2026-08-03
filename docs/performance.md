# Release 性能实测

测量日期：2026-08-03（Australia/Sydney）  
系统：Windows NT 10.0.26200.0，x64，32 个逻辑处理器  
编译器：MSVC 19.44.35228（Visual Studio 2022 v17 工具集）  
SDK：Windows SDK 10.0.26100.0  
构建：Release，`/O2 /GL /LTCG /OPT:REF /OPT:ICF`，静态 CRT

## 结果

| 指标 | 实测 | 说明 |
|---|---:|---|
| 监视器 EXE 大小 | 11,738,624 bytes（约 11.19 MB） | 静态 CRT；内嵌用户指定的 1689×931 红灯原图及三个同源高分辨率状态版本；官方 CLI 为独立随附文件 |
| 启动到 HUD 窗口 | 193.9 ms | 包含 CreateProcess/窗口发现测量开销 |
| 空闲工作集 | 9.03 MB | 首次绘制后 5 秒，正常蓝灯 |
| 空闲私有内存 | 9.94 MB | 同上 |
| 30 秒空闲 CPU 时间 | 0.046875 s | 正常蓝灯，无动画定时器 |
| 空闲 CPU（整机） | 0.0049% | 32 逻辑处理器折算；低于 0.1% 目标 |
| 空闲 CPU（单逻辑核口径） | 0.1562% | 仅供对照，受 15.625 ms 计时粒度影响 |
| Fake App Server 首次刷新 | 18.03 ms | 包含子进程启动、握手和首个请求 |
| Fake App Server 后续刷新均值 | 0.0316 ms | 10 次本机 JSONL 往返 |
| Fake App Server 工作集 | 3.18 MB | C++ 测试替身，不代表官方 CLI |
| Fake App Server 私有内存 | 0.46 MB | 同上 |
| 官方 Codex CLI 首次真实额度读取 | 676.36 ms | 0.146.0，含子进程启动、握手和网络请求 |
| 官方 Codex CLI 工作集 | 42.05 MB | 收到真实额度响应后 |
| 官方 Codex CLI 私有内存 | 20.54 MB | 收到真实额度响应后 |
| ChatGPT 窗口关闭退出延迟 | 5 秒防抖 + 消息调度 | 由单次定时器实现，无轮询 |

## 实际运行记录

Release EXE 已实际运行并可视核对以下状态：

- 55%：蓝色圆核稳定常亮，300×90 紧凑 HUD；
- 10%：红色玻璃警戒，亮/酒红暗相位按 0.4 秒完整周期闪烁；
- 0%：胸甲与灯体统一为低对比哑光石像，静止；
- 最小化/托盘恢复交互路径；
- `--check-environment` 执行后无监视器残留进程。
- 真正的 `--launcher` 已连接当前运行中的 ChatGPT；没有重复启动 ChatGPT，监视器工作集 10.41 MB、私有内存 10.09 MB，测试后无监视器残留进程。

本机 ChatGPT 实际由 `OpenAI.Codex_2p2nqsd0c76g0!App` 提供，开始菜单显示名为 ChatGPT。动态 AppListEntry 识别已覆盖这种包名变化。

## 官方 App Server 实测

Microsoft Store 包内的 CLI 受到 WindowsApps 权限保护，普通桌面进程无法直接启动。交付包因此附带经官方发布页 SHA-256 校验的 Codex CLI 0.146.0。已使用这一二进制完成 `initialize`/`initialized`、`account/read` 和 `account/rateLimits/read` 真实 JSONL 往返，成功返回当前 ChatGPT Plus 账户的 `codex` 额度窗口。测试只验证字段和数值范围，文档与日志不保存邮箱、令牌或完整账户响应。Fake App Server 继续覆盖 0%、单/双窗口、unlimited、credits、401、429、超时、断连和 notification 等难以安全制造的边界场景。

为了避免关闭当前正在承载本次 Codex 会话的 ChatGPT 桌面应用，未执行“关闭真实 ChatGPT 后监视器退出”的破坏性端到端动作。其窗口消失检测、最小化/隐藏不误判、进程等待和重复 Mutex 已由生命周期测试覆盖，最终退出防抖固定为 5 秒。

## 优化核对

- ChatGPT 生命周期没有 100 ms/1 s 进程轮询；
- 正常蓝灯、石化和数据不可用状态没有动画 Timer；
- 红灯只在 on/off 翻转时重绘，不存在永久 60 FPS 循环；
- “致以光辉的人”背板使用静态 Direct2D 光点、光束和贝塞尔光轨，不新增动画 Timer；
- 量子字体的等离子渐变、双层辉光和结构化离子随原有绘制完成，警告态只复用红灯 Timer，不新增动画 Timer；
- 额度主动刷新默认 60 秒且同一时间只有一个请求；
- App Server 每次监视器会话只创建一次；
- 管道读取线程阻塞在 `ReadFile`，不忙等；
- 日志默认不持续写，仅 warning/error，1 MB × 3 轮转；
- Direct2D 使用软件 HwndRenderTarget，首次绘制后回收不活跃工作集；
- Release 依赖检查未发现 `VCRUNTIME`/`MSVCP` 外部运行库。
