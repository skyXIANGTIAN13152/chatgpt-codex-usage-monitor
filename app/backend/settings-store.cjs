'use strict';
const { t } = require('./i18n.cjs');
const fs = require('node:fs');
const path = require('node:path');

const DEFAULT_SETTINGS = Object.freeze({
  visible: Object.freeze({ codex: true, claude: true }),
  energyTarget: 'codex', aggregateMembers: Object.freeze(['codex', 'claude']),
  quotaDisplay: 'bars',
  launchWithCodex: false, launchWithClaude: false,
  alwaysOnTop: true, compact: true, reducedMotion: false, followSystemMotion: true, paused: false,
  warningThreshold: 20, refreshSeconds: 60, floatingScale: 100,
});
const BOOLEAN_KEYS = ['launchWithCodex', 'launchWithClaude', 'alwaysOnTop', 'compact', 'reducedMotion', 'followSystemMotion', 'paused'];
const NUMBER_RANGES = { warningThreshold: [1, 99], refreshSeconds: [30, 3600] };

function plainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value) &&
    (Object.getPrototypeOf(value) === Object.prototype || Object.getPrototypeOf(value) === null);
}

function validatePatch(patch) {
  if (!plainObject(patch)) throw new TypeError(t('设置必须是对象。'));
  const allowed = new Set(['visible', 'energyTarget', 'aggregateMembers', 'quotaDisplay', 'floatingScale', ...BOOLEAN_KEYS, ...Object.keys(NUMBER_RANGES)]);
  const sanitized = {};
  for (const [key, value] of Object.entries(patch)) {
    if (!allowed.has(key)) throw new TypeError(t('包含不支持的设置。'));
    if (key === 'visible') {
      if (!plainObject(value) || Object.keys(value).some(id => !['codex', 'claude'].includes(id))) {
        throw new TypeError(t('显示选项无效。'));
      }
      sanitized.visible = {};
      for (const [id, enabled] of Object.entries(value)) {
        if (typeof enabled !== 'boolean') throw new TypeError(t('显示选项必须为开关。'));
        sanitized.visible[id] = enabled;
      }
    } else if (key === 'energyTarget') {
      if (!['codex', 'claude', 'aggregate'].includes(value)) throw new TypeError(t('能量来源无效。'));
      sanitized.energyTarget = value;
    } else if (key === 'quotaDisplay') {
      if (!['bars', 'rings'].includes(value)) throw new TypeError(t('额度显示形式无效。'));
      sanitized.quotaDisplay = value;
    } else if (key === 'aggregateMembers') {
      if (!Array.isArray(value) || value.length < 1 || value.length > 2 ||
          new Set(value).size !== value.length || [...value].some(id => !['codex', 'claude'].includes(id))) {
        throw new TypeError(t('综合额度至少需要选择一个不同的有效账号。'));
      }
      sanitized.aggregateMembers = ['codex', 'claude'].filter(id => value.includes(id));
    } else if (key === 'floatingScale') {
      if (![80, 100, 125, 150].includes(value)) throw new TypeError(t('悬浮窗大小无效。'));
      sanitized.floatingScale = value;
    } else if (BOOLEAN_KEYS.includes(key)) {
      if (typeof value !== 'boolean') throw new TypeError(t('设置值必须为开关。'));
      sanitized[key] = value;
    } else {
      const [minimum, maximum] = NUMBER_RANGES[key];
      if (!Number.isInteger(value) || value < minimum || value > maximum) throw new TypeError(t('设置数值超出范围。'));
      sanitized[key] = value;
    }
  }
  return sanitized;
}

function mergeSettings(current, patch) {
  const valid = validatePatch(patch);
  return { ...current, ...valid, visible: { ...current.visible, ...valid.visible } };
}

function readSafeSettings(value) {
  let settings = structuredClone(DEFAULT_SETTINGS);
  if (!plainObject(value)) return settings;
  for (const key of Object.keys(DEFAULT_SETTINGS)) {
    if (!Object.hasOwn(value, key)) continue;
    try { settings = mergeSettings(settings, { [key]: value[key] }); } catch {}
  }
  // Upgrade pre-target preferences once: preserve a sole visible account;
  // both visible (or neither visible) starts with Codex. Later visibility
  // changes never change the selected energy target.
  if (!Object.hasOwn(value, 'energyTarget')) {
    const visible = ['codex', 'claude'].filter(id => settings.visible[id]);
    settings.energyTarget = visible.length === 1 ? visible[0] : 'codex';
  }
  return settings;
}

class SettingsStore {
  constructor({ dataDir, legacyPath = null } = {}) {
    if (!dataDir) throw new TypeError('An application data directory is required.');
    this.dataDir = path.resolve(dataDir);
    this.settingsPath = path.join(this.dataDir, 'settings.json');
    this.legacyPath = legacyPath;
    this.settings = structuredClone(DEFAULT_SETTINGS);
  }

  load() {
    fs.mkdirSync(this.dataDir, { recursive: true });
    if (fs.existsSync(this.settingsPath)) {
      try {
        const saved = JSON.parse(fs.readFileSync(this.settingsPath, 'utf8'));
        this.settings = readSafeSettings(saved);
        if (plainObject(saved) && (!Object.hasOwn(saved, 'energyTarget') || !Object.hasOwn(saved, 'aggregateMembers')
            || saved.quotaDisplay !== this.settings.quotaDisplay || Object.hasOwn(saved, 'startupAnimation'))) {
          // A migration save can fail independently of reading valid settings.
          // Keep the loaded preferences; the next successful write persists it.
          try { this.write(this.settings); } catch {}
        }
      }
      catch { this.settings = structuredClone(DEFAULT_SETTINGS); }
    } else {
      // Only these non-account fields are migrated. No credential source is read.
      if (this.legacyPath && fs.existsSync(this.legacyPath)) {
        try {
          const legacy = JSON.parse(fs.readFileSync(this.legacyPath, 'utf8'));
          for (const key of ['refreshSeconds', 'warningThreshold', 'alwaysOnTop']) {
            if (Object.hasOwn(legacy, key)) {
              try { this.settings = mergeSettings(this.settings, { [key]: legacy[key] }); } catch {}
            }
          }
        } catch {}
      }
      this.write(this.settings);
    }
    return this.get();
  }

  get() { return structuredClone(this.settings); }

  write(settings) {
    fs.mkdirSync(this.dataDir, { recursive: true });
    const temporaryPath = `${this.settingsPath}.${process.pid}.tmp`;
    fs.writeFileSync(temporaryPath, `${JSON.stringify(settings, null, 2)}\n`, { encoding: 'utf8', mode: 0o600 });
    try { fs.renameSync(temporaryPath, this.settingsPath); }
    catch (error) {
      try { fs.unlinkSync(temporaryPath); } catch {}
      throw error;
    }
  }

  update(patch) {
    const next = mergeSettings(this.settings, patch);
    this.write(next);
    this.settings = next;
    return this.get();
  }
}

module.exports = { SettingsStore, DEFAULT_SETTINGS, validatePatch, mergeSettings, readSafeSettings };
