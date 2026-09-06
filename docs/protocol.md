# Codex App Server 协议记录

本项目实现依据 2026-08-26 由当前 Codex CLI 生成的 App Server Schema 及官方文档。当前协议为省略 `jsonrpc: "2.0"` 头的双向 JSON-RPC；stdio 使用一行一个 JSON 对象的 JSONL。客户端每次连接只发送一次 `initialize`，随后发送 `initialized` 通知，握手完成后才调用账户方法。

额度请求：

```json
{"method":"account/rateLimits/read","id":10}
```

主动更新：

```json
{"method":"account/rateLimits/updated","params":{"rateLimits":{}}}
```

`rateLimits` 是向后兼容单 bucket 视图；存在有效 `rateLimitsByLimitId` 对象时以它为准，即使它为空，也不会退回旧视图复活已移除的窗口。显示端只选择 `limitId = codex` 的主额度，绝不借用 `codex_bengalfox` / Spark 或其他模型窗口来凑齐双周期。旧协议缺少 ID 的单视图按 `codex` 处理。模型 bucket 的套餐和 credits 元数据也不覆盖主额度。

`primary`/`secondary` 分别展开为独立 `RateWindow`，但不把槽位名称当作业务含义：`windowDurationMins = 300` 识别为 5 小时窗口，`10080` 识别为周窗口。主额度只返回周周期时自动使用单圈 / 单条；同时返回两个周期时恢复双圈 / 双条，不改用户配置。`usedPercent` 只换算为 `clamp(100-usedPercent,0,100)`；`resetsAt` 支持 Unix秒、Unix 毫秒或 ISO 8601 并统一为 UTC。

`account/rateLimits/updated` 按稀疏更新处理。程序记录通知中明确出现的 `limitId + primary/secondary`：省略的字段保持缓存；明确为 null 的字段清除旧窗口；有效对象替换对应槽位。升级时周额度从 `secondary` 移到 `primary` 也不会留下旧的 5 小时额度。只涉及 Spark 的通知不会刷新主额度的新鲜度、灯效或数据。完整 `account/rateLimits/read` 响应替换整份快照。有效的空额度响应被表示为“暂不可用”，与网络 / 协议错误区分，不虚构 0% 或 100%。

2026-09-06 的回归覆盖：主额度仅周 + Spark 双周期、主额度归零 + Spark 满额、反向槽位、升级稀疏通知、明确 null / 省略字段、空完整响应、只有模型 bucket、断网、自动单双切换及尺寸 / 位置保持。真实额度诊断输出包括 bucket ID，并接受合法的单周期结果。

参考：[Codex App Server 官方文档](https://developers.openai.com/codex/app-server/)。CLI 也可用 `codex app-server generate-json-schema --out <目录>` 生成与安装版本精确匹配的 Schema。
