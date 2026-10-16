'use strict';

// The release variant selects a fixed language; OS and shell locale never do.
const APP_LOCALE = require('../package.json').appLocale === 'en' ? 'en' : 'zh-CN';
const ENGLISH_MESSAGES = Object.freeze({
  "操作未能完成，请重试。": "The action could not be completed. Please try again.",
  "链接未通过官方地址检查。": "This link is not an approved official address.",
  "自动唤起组件未就绪。": "The automatic launch component is not available.",
  "自动唤起设置未能保存。": "Automatic launch settings could not be saved.",
  "打开额度显示器": "Open Tiga Energy Monitor",
  "账号与设置": "Accounts and Settings",
  "始终置顶": "Always on Top",
  "退出额度显示器": "Quit Tiga Energy Monitor",
  "此窗口不能执行该操作。": "This window is not allowed to perform that action.",
  "账号类型无效。": "Invalid account provider.",
  "预览测试中未启用账号服务。": "Account services are disabled in this test session.",
  "只有悬浮窗能更新点击区域。": "Only the floating widget can update its interactive area.",
  "窗口操作无效。": "Invalid window action.",
  "迪迦额度显示器": "Tiga Energy Monitor",
  "迪迦额度显示器 · 账号与设置": "Tiga Energy Monitor — Accounts and Settings",
  "Claude Code 读取服务已断开。": "The Claude Code usage service has disconnected.",
  "Claude Code 读取服务已断开，请刷新重试。": "The Claude Code usage service has disconnected. Refresh to try again.",
  "Claude Code 请求未完成。": "The Claude Code request could not be completed.",
  "Claude Code 响应超时，请重试。": "Claude Code did not respond in time. Please try again.",
  "Claude Code 读取服务不可用。": "The Claude Code usage service is unavailable.",
  "Claude Code 服务已停止。": "The Claude Code service has stopped.",
  "5 小时": "5 hours",
  "每周": "Weekly",
  "总周": "Weekly total",
  "每周 · Sonnet": "Weekly · Sonnet",
  "其他额度": "Other quota",
  "额度": "Quota",
  "使用官方 Claude Code 登录后读取额度。": "Sign in through the official Claude Code app to read your usage.",
  "额度窗口已重置，等待官方数据刷新。": "This quota period has reset. Waiting for updated official data.",
  "尚未安装官方 Claude Code 组件，可先打开官方用量页。": "The official Claude Code component is not installed. You can open the official usage page.",
  "官方 Claude Code 响应超时，请重试。": "The official Claude Code app did not respond in time. Please try again.",
  "无法启动官方 Claude Code 组件。": "The official Claude Code component could not be started.",
  "官方 Claude Code 组件缺失。": "The official Claude Code component is missing.",
  "可在官方用量页登录并查看额度；这不会自动连接到显示器。": "You can sign in and view usage on the official usage page. This does not connect it to the monitor.",
  "请在官方 Claude Code 打开的浏览器中完成登录。": "Complete sign-in in the browser opened by the official Claude Code app.",
  "登录等待超时，请重新点击登录。": "Sign-in timed out. Please select Sign in again.",
  "官方登录未完成，请重新登录。": "Official sign-in was not completed. Please sign in again.",
  "也可以打开官方用量页查看额度。": "You can also view your quota on the official usage page.",
  "官方 Claude Code 未返回可识别的登录状态。": "The official Claude Code app returned an unrecognized sign-in status.",
  "使用 Claude Code 账号登录。": "Sign in with your Claude Code account.",
  "当前官方组件使用 API 计费方式，未提供订阅剩余额度。": "The official component is using API billing and has not provided subscription quota.",
  "正在读取官方 Claude Code 用量界面。": "Reading the official Claude Code usage screen.",
  "来自官方 Claude Code 用量界面。": "From the official Claude Code usage screen.",
  "已登录；暂未读取到额度，可打开官方用量页查看。": "Signed in, but quota is not available yet. You can open the official usage page.",
  "Claude Code 额度读取失败。": "Claude Code quota could not be read.",
  "可重试，或打开官方用量页。": "Try again or open the official usage page.",
  "用量读取组件不可用，请打开官方用量页查看。": "The usage reader is unavailable. Please open the official usage page.",
  "已登录；官方用量界面尚未就绪，可打开官方用量页查看。": "Signed in, but the official usage screen is not ready. You can open the official usage page.",
  "请完成官方 Claude Code 首次设置，完成后刷新额度。": "Complete the official Claude Code first-time setup, then refresh your quota.",
  "无法打开官方用量界面，请打开官方用量页查看。": "The official usage screen could not be opened. Please open the official usage page.",
  "这份官方状态行数据没有有效的订阅额度。": "This official status line contains no valid subscription quota.",
  "来自官方 Claude Code 状态行；显示最近一次收到的数据。": "From the official Claude Code status line. Showing the most recently received data.",
  "请在已打开的官方终端完成首次设置；关闭终端后会自动刷新。": "Complete first-time setup in the official terminal that opened. Usage will refresh when you close it.",
  "官方设置窗口未能完成，请重试或打开官方用量页。": "Official setup was not completed. Try again or open the official usage page.",
  "官方组件退出登录失败，请重试。": "The official component could not sign out. Please try again.",
  "无法启动随附的 Codex 服务。": "The bundled Codex service could not be started.",
  "Codex 服务响应超时，请重试。": "The Codex service did not respond in time. Please try again.",
  "登录已超时，请重新登录。": "Sign-in timed out. Please sign in again.",
  "登录未完成，请重新登录。": "Sign-in was not completed. Please sign in again.",
  "登录地址未通过官方域名检查。": "The sign-in address is not on an approved official domain.",
  "无法打开浏览器，请检查默认浏览器后重试。": "The browser could not be opened. Check your default browser and try again.",
  "登录已过期，请重新登录。": "Your sign-in has expired. Please sign in again.",
  "Codex 服务已断开，请刷新重试。": "The Codex service has disconnected. Refresh to try again.",
  "暂时无法读取 Codex 数据，请稍后重试。": "Codex data is temporarily unavailable. Please try again later.",
  "官方暂未提供额度数据。": "The official service has not provided quota data yet.",
  "悬浮窗大小无效。": "Invalid floating widget size.",
  "点击区域数量无效。": "Invalid number of interactive regions.",
  "点击区域必须是有效矩形。": "Interactive regions must be valid rectangles.",
  "点击区域超出悬浮窗范围。": "An interactive region extends beyond the floating widget.",
  "设置必须是对象。": "Settings must be an object.",
  "包含不支持的设置。": "The request contains unsupported settings.",
  "显示选项无效。": "Invalid display options.",
  "显示选项必须为开关。": "Display options must be boolean values.",
  "能量来源无效。": "Invalid energy source.",
  "额度显示形式无效。": "Invalid quota display mode.",
  "综合额度至少需要选择一个不同的有效账号。": "Select at least one valid account for combined quota, without duplicates.",
  "设置值必须为开关。": "This setting must be a boolean value.",
  "设置数值超出范围。": "The setting value is outside the allowed range."
});

function t(message, locale = APP_LOCALE) {
  return locale === 'en' ? (ENGLISH_MESSAGES[message] ?? message) : message;
}

// Local OS errors can be in another language. Preserve diagnostics in their
// own language for Chinese builds, but never leak untranslated Han into English UI.
// Account names, plan names and official protocol values never pass through this.
function localizedError(error, fallback = '操作未能完成，请重试。', locale = APP_LOCALE) {
  const message = typeof error === 'string' ? error : error?.message;
  if (typeof message !== 'string' || !message.trim()) return t(fallback, locale);
  if (Object.hasOwn(ENGLISH_MESSAGES, message)) return t(message, locale);
  return locale === 'en' && /\p{Script=Han}/u.test(message) ? t(fallback, locale) : message;
}

module.exports = { APP_LOCALE, ENGLISH_MESSAGES, t, localizedError };

