'use strict';

const { spawn } = require('node:child_process');

function rpcError(code) {
  const error = new Error(code);
  error.code = code;
  return error;
}

// The transport deliberately never logs server output or forwards server error text.
class RpcClient {
  constructor({ executable, args, cwd, env, spawnProcess = spawn, timeoutMs = 15000,
    onNotification = () => {}, onClose = () => {} }) {
    this.timeoutMs = timeoutMs;
    this.onNotification = onNotification;
    this.onClose = onClose;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = '';
    this.closed = false;
    this.intentional = false;
    this.child = spawnProcess(executable, args, {
      cwd, env, stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true, shell: false,
    });
    this.child.stdout.setEncoding('utf8');
    this.child.stdout.on('data', (chunk) => this.receive(chunk));
    this.child.stderr.on('data', () => {});
    this.child.stdin.on('error', () => this.fail('CLOSED'));
    this.child.on('error', () => this.fail('LAUNCH_FAILED'));
    this.child.on('exit', () => this.fail('CLOSED'));
  }

  request(method, params, timeoutMs = this.timeoutMs) {
    if (this.closed) return Promise.reject(rpcError('CLOSED'));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(rpcError('TIMEOUT'));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      try {
        const message = { id, method };
        if (params !== undefined) message.params = params;
        this.child.stdin.write(JSON.stringify(message) + '\n');
      } catch {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(rpcError('CLOSED'));
      }
    });
  }

  notify(method, params) {
    if (this.closed) throw rpcError('CLOSED');
    const message = { method };
    if (params !== undefined) message.params = params;
    this.child.stdin.write(JSON.stringify(message) + '\n');
  }

  receive(chunk) {
    if (this.closed) return;
    this.buffer += chunk;
    if (this.buffer.length > 1024 * 1024) return this.fail('PROTOCOL');
    let newline;
    while ((newline = this.buffer.indexOf('\n')) !== -1) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (!line || !line.startsWith('{')) continue;
      let message;
      try { message = JSON.parse(line); } catch { continue; }
      if (!message || typeof message !== 'object') continue;
      if (message.method && message.id !== undefined) {
        // No tool/approval requests are allowed in this read-only integration.
        this.child.stdin.write(JSON.stringify({ id: message.id,
          error: { code: -32601, message: 'Method not supported' } }) + '\n');
      } else if (typeof message.method === 'string') {
        this.onNotification(message.method, message.params || {});
      } else if (this.pending.has(message.id)) {
        const pending = this.pending.get(message.id);
        this.pending.delete(message.id);
        clearTimeout(pending.timer);
        if (message.error) {
          const code = Number(message.error.code);
          pending.reject(rpcError(code === 401 || code === 403 ? 'UNAUTHORIZED' : 'RPC_FAILED'));
        } else {
          pending.resolve(message.result);
        }
      }
    }
  }

  fail(code) {
    if (this.closed) return;
    this.closed = true;
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(rpcError(code));
    }
    this.pending.clear();
    if (!this.intentional) this.onClose(code);
  }

  stop() {
    this.intentional = true;
    this.fail('STOPPED');
    try { this.child.stdin.end(); } catch {}
    try { this.child.kill(); } catch {}
  }
}

module.exports = { RpcClient, rpcError };
