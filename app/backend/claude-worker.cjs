'use strict';
const { localizedError } = require('./i18n.cjs');

const readline = require('node:readline');
const { ClaudeProvider } = require('./claude-provider.cjs');

// Run with the bundled standalone Node runtime, not Electron's native ABI.
// node claude-worker.cjs --executable <official exe> --data-dir <isolated app dir>
function argument(name) {
  const index = process.argv.indexOf(name);
  return index < 0 ? null : process.argv[index + 1];
}
function send(message) { process.stdout.write(`${JSON.stringify(message)}\n`); }

const provider = new ClaudeProvider({
  executable: argument('--executable'), dataDir: argument('--data-dir'),
  onUpdate: state => send({ event: 'state', state }),
  openExternal: url => send({ event: 'openExternal', url }),
});
const methods = new Set(['start', 'login', 'refresh', 'logout', 'stop', 'getState', 'openUsagePage', 'openSetup', 'importStatusLine']);
const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on('line', async line => {
  if (line.length > 1024 * 1024) return;
  let request;
  try {
    request = JSON.parse(line);
    if (!methods.has(request.method)) throw new Error('Unknown provider method.');
    const result = await provider[request.method](request.params);
    send({ id: request.id, result });
  } catch (error) {
    send({ id: request?.id ?? null, error: localizedError(error, 'Claude Code 请求未完成。') });
  }
});
input.on('close', () => { provider.stop(); process.exit(0); });
process.on('SIGTERM', () => { provider.stop(); process.exit(0); });
process.on('SIGINT', () => { provider.stop(); process.exit(0); });
