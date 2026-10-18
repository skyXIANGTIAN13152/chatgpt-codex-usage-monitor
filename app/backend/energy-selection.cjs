'use strict';
const { t } = require('./i18n.cjs');

const PROVIDER_IDS = ['codex', 'claude'];
const USABLE_STATUSES = new Set(['connected', 'connecting', 'error']);
const PERIODS = [
  { id: 'short', label: t('5 小时'), windowMinutes: 300 },
  { id: 'weekly', label: t('总周'), windowMinutes: 10080 },
];
const MODEL_SPECIFIC_WINDOW = /(?:^|[^a-z0-9])(?:sonnet|opus|haiku|spark|bengalfox)(?=$|[^a-z])|(?:^|[^a-z0-9])(?:gpt|claude)[-\d]/i;

function modelSpecific(window) {
  return MODEL_SPECIFIC_WINDOW.test(`${window?.id ?? ''} ${window?.label ?? ''}`);
}

function validWindow(window, now) {
  if (!window || typeof window !== 'object') return false;
  const value = window.remainingPercent;
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0 || value > 100) return false;
  if (window.resetsAt !== null && window.resetsAt !== undefined) {
    const reset = Date.parse(window.resetsAt);
    if (!Number.isFinite(reset) || reset <= now) return false;
  }
  return true;
}

function quotaMaxAgeMs(settings) {
  const value = settings?.refreshSeconds;
  const interval = Number.isInteger(value) && value >= 30 && value <= 3600 ? value : 60;
  // Keep readings through the next scheduled refresh plus time for retries.
  // The renderer applies the same policy; reset deadlines are still immediate.
  return Math.max(180000, interval * 1000 + 120000);
}

function providerQuota(provider, now, maxAgeMs) {
  const rawWindows = Array.isArray(provider?.windows) ? provider.windows : [];
  const accountWindows = rawWindows.filter(window => window && typeof window === 'object'
    && !Array.isArray(window) && !modelSpecific(window));
  const timestamp = Date.parse(provider?.updatedAt);
  const age = now - timestamp;
  const fresh = USABLE_STATUSES.has(provider?.status) && Number.isFinite(age) && age >= 0 && age <= maxAgeMs;
  return { rawWindows, accountWindows,
    windows: fresh ? accountWindows.filter(window => validWindow(window, now)) : [] };
}

function targetMembers(settings, mode) {
  if (mode !== 'aggregate') return [mode];
  const members = settings?.aggregateMembers ?? PROVIDER_IDS;
  if (!Array.isArray(members) || members.length < 1 || members.length > PROVIDER_IDS.length ||
      new Set(members).size !== members.length || [...members].some(id => !PROVIDER_IDS.includes(id))) return [];
  return PROVIDER_IDS.filter(id => members.includes(id));
}

function preferredPeriod(quotas) {
  // Choose from declarations, before freshness/value filtering. A declared but
  // unusable five-hour limit must never fall back to a healthy weekly quota.
  return PERIODS.find(period => quotas.some(quota => quota.accountWindows.some(
    window => window.windowMinutes === period.windowMinutes))) || null;
}

function periodRemaining(quota, period) {
  const declared = quota.accountWindows.filter(window => window.windowMinutes === period.windowMinutes);
  const valid = quota.windows.filter(window => window.windowMinutes === period.windowMinutes);
  if (!declared.length || valid.length !== declared.length) return null;
  // Multiple account-wide records still give an account one contribution.
  return Math.min(...valid.map(window => window.remainingPercent));
}

function singleTarget(mode, memberIds, quota) {
  const period = preferredPeriod([quota]);
  const valid = new Set(quota.windows);
  const windows = quota.accountWindows.map((window, index) => {
    const knownPeriod = PERIODS.find(period => period.windowMinutes === window.windowMinutes);
    return {
      id: typeof window.id === 'string' && window.id ? window.id : `window-${index + 1}`,
      label: knownPeriod?.label || (typeof window.label === 'string' && window.label ? window.label : t('额度')),
      windowMinutes: typeof window.windowMinutes === 'number' && Number.isFinite(window.windowMinutes)
        && window.windowMinutes > 0 ? window.windowMinutes : null,
      remainingPercent: valid.has(window) ? window.remainingPercent : null,
      memberCount: valid.has(window) ? 1 : 0, totalCount: 1,
    };
  });
  const remaining = period ? periodRemaining(quota, period)
    : quota.windows.length ? Math.min(...quota.windows.map(window => window.remainingPercent)) : null;
  const ready = remaining !== null;
  return { mode, memberIds, controllingPeriod: period?.id || null, remainingPercent: remaining,
    windows, missingIds: ready ? [] : [...memberIds], status: ready ? 'ready' : 'incomplete',
    ...(ready ? {} : { reason: period ? 'missing_period_data' : 'quota_unavailable' }) };
}

function selectEnergyTarget(providers, settings, now = Date.now()) {
  const mode = [...PROVIDER_IDS, 'aggregate'].includes(settings?.energyTarget) ? settings.energyTarget : 'codex';
  const memberIds = targetMembers(settings, mode);
  if (!memberIds.length) return { mode, memberIds, controllingPeriod: null, remainingPercent: null, windows: [], missingIds: [],
    status: 'incomplete', reason: 'invalid_members' };

  const maxAgeMs = quotaMaxAgeMs(settings);
  const quotas = new Map(memberIds.map(id => [id, providerQuota(providers?.[id], now, maxAgeMs)]));
  // One aggregate member behaves exactly like selecting that account, including
  // valid windows whose duration is not a known comparison period.
  if (memberIds.length === 1) return singleTarget(mode, memberIds, quotas.get(memberIds[0]));

  const period = preferredPeriod([...quotas.values()]);
  const contributionsByPeriod = new Map();
  const windows = [];
  for (const item of PERIODS) {
    const relevant = memberIds.some(id => quotas.get(id).accountWindows.some(
      window => window.windowMinutes === item.windowMinutes));
    if (!relevant) continue;
    const members = new Map(memberIds.map(id => [id, periodRemaining(quotas.get(id), item)]));
    contributionsByPeriod.set(item.id, members);
    const contributions = [...members.values()].filter(value => value !== null);
    const complete = contributions.length === memberIds.length;
    windows.push({ ...item, remainingPercent: complete
      ? contributions.reduce((sum, value) => sum + value, 0) / memberIds.length : null,
    memberCount: contributions.length, totalCount: memberIds.length });
  }

  // Other periods remain informational. Only the declared preferred period
  // needs all selected members; no partial average substitutes for a missing one.
  const control = windows.find(window => window.id === period?.id);
  const missingIds = period ? memberIds.filter(id => contributionsByPeriod.get(period.id).get(id) === null) : [...memberIds];
  const ready = !!control && missingIds.length === 0;
  const unsupported = [...quotas.values()].some(quota => quota.accountWindows.length > 0);
  return { mode, memberIds, controllingPeriod: period?.id || null,
    remainingPercent: ready ? control.remainingPercent : null,
    windows, missingIds, status: ready ? 'ready' : 'incomplete',
    ...(ready ? {} : { reason: period ? 'missing_period_data' : unsupported ? 'unsupported_periods' : 'no_matching_periods' }) };
}

function selectFreshRemaining(providers, settings, now = Date.now()) {
  return selectEnergyTarget(providers, settings, now).remainingPercent;
}

module.exports = { selectEnergyTarget, selectFreshRemaining, quotaMaxAgeMs };
