'use strict';
const { t } = require('./i18n.cjs');

const path = require('node:path');
const { RpcClient } = require('./rpc-client.cjs');

class CoreBridge {
  constructor({ executable = path.join(__dirname, '..', 'runtime', 'monitor-core.exe'),
    spawnProcess } = {}) {
    this.executable = executable;
    this.spawnProcess = spawnProcess;
    this.rpc = null;
  }

  connect() {
    if (!this.rpc || this.rpc.closed) this.rpc = new RpcClient({ executable: this.executable,
      args: [], cwd: path.dirname(this.executable), env: {},
      spawnProcess: this.spawnProcess, timeoutMs: 5000 });
    return this.rpc;
  }

  async parseRateLimits(payload, sparse = false) {
    const message = sparse ? { method: 'account/rateLimits/updated', params: payload } : { result: payload };
    const result = await this.connect().request('parseRateLimits', { message: JSON.stringify(message) });
    return { windows: result.windows.map((window) => ({ ...window,
      label: t(window.label),
      resetsAt: typeof window.resetsAt === 'number' ? new Date(window.resetsAt * 1000).toISOString() : null,
    })), plan: result.plan };
  }

  computeEnergy(remainingPercent, options = {}) {
    return this.connect().request('computeEnergy', { remainingPercent, ...options });
  }

  async classifyEnergy(remainingPercent, warningThreshold = 50) {
    const energy = await this.computeEnergy(remainingPercent, { warningThreshold });
    const scene = { normal: 'full', warning: 'low', critical: 'low', exhausted: 'stone',
      unavailable: 'unknown' }[energy.visual] || 'unknown';
    return { scene, ...energy };
  }

  async clear() {
    if (this.rpc && !this.rpc.closed) await this.rpc.request('clear');
  }

  stop() {
    if (this.rpc) this.rpc.stop();
    this.rpc = null;
  }
}

module.exports = { CoreBridge };
