# Codex App Server 协议记录

本项目实现依据 2026-08-02 获取的当前 Codex 官方 App Server 文档。当前协议为省略 `jsonrpc: "2.0"` 头的双向 JSON-RPC；stdio 使用一行一个 JSON 对象的 JSONL。客户端每次连接只发送一次 `initialize`，随后发送 `initialized` 通知，握手完成后才调用账户方法。

额度请求：

```json
{"method":"account/rateLimits/read","id":10}
```

主动更新：

```json
{"method":"account/rateLimits/updated","params":{"rateLimits":{}}}
```

`rateLimits` 是向后兼容单 bucket 视图；存在 `rateLimitsByLimitId` 时优先使用它。`primary`/`secondary` 分别展开为独立 `RateWindow`。`usedPercent` 只换算为 `clamp(100-usedPercent,0,100)`；`resetsAt` 支持 Unix 秒、Unix 毫秒或 ISO 8601 并统一为 UTC。

参考：[Codex App Server 官方文档](https://developers.openai.com/codex/app-server/)。CLI 也可用 `codex app-server generate-json-schema --out <目录>` 生成与安装版本精确匹配的 Schema。

