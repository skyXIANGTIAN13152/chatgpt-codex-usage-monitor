# v1.0.5 / v1.0.5-en

## 中文

- 圆环整体右移，与迪迦胸甲留出间距；单圈、双圈一致，窗口尺寸不变。
- 胸甲自然融入光能背板，优化数字、时间、底栏按钮、分组菜单及悬停提示。
- 只选择 Codex 主额度；自动适配实际返回的单周期 / 双周期，不使用 Spark 等模型的独立额度补齐。
- 缺失周期不再显示假 100%；无主额度显示暂不可用，断网明确标注缓存状态。
- 主额度没有更新时，不因其他模型的通知刷新时间；稀疏通知保留未变动周期并正确处理清空字段。
- 保留原有位置、缩放和显示偏好；仅胸灯闪烁，圆环和进度条保持稳定。
- 加入独立示例预览；不连接账户，不修改正式设置或日志。
- 中英文版分别编译并运行五项测试套件；提供绿色包、源码包和 SHA-256 校验文件。

## English

- Move single and nested rings clear of the chest artwork without enlarging the HUD.
- Blend the chest into the light-energy background and refine values, reset text, footer controls, grouped menus, and hover details.
- Select only the main Codex quota and adapt to its actual single/dual periods; never fill gaps with Spark or other model-specific quotas.
- Do not turn missing periods into a false 100%; explicitly distinguish unavailable data and offline cached values.
- Unrelated model notifications cannot refresh the main quota's age; sparse updates preserve omitted fields and correctly clear explicit empty fields.
- Keep saved position, scale, and display preferences. Rings and bars stay steady; only the chest lamp flashes.
- Add isolated sample previews without an account connection or changes to live settings/logs.
- Build and test both editions separately; provide portable ZIPs, source ZIPs, and SHA-256 checksums.

The default / Latest release is Chinese (`v1.0.5`). The fully English edition is published separately as `v1.0.5-en` with the same functionality.
