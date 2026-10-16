'use strict';
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const workspace = path.resolve(root, '..', '..');
const stagingRoot = path.join(root, '..', 'package-staging');
const copy = (source, destination, options = {}) => fs.cpSync(source, destination, { recursive: true, dereference: true, ...options });

function isWithin(candidate, directory) {
  const relative = path.relative(directory, candidate);
  return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative));
}

function resolveExistingPath(candidate) {
  let existing = candidate;
  while (!fs.existsSync(existing)) existing = path.dirname(existing);
  return path.resolve(fs.realpathSync(existing), path.relative(existing, candidate));
}

function validateOutputPath(value) {
  const target = path.resolve(value);
  if (!isWithin(target, workspace) || target === workspace ||
      isWithin(root, target) || isWithin(target, root) ||
      isWithin(stagingRoot, target) || isWithin(target, stagingRoot)) {
    throw new Error('Choose an output directory inside this workspace and outside the source and staging trees.');
  }
  const resolved = resolveExistingPath(target);
  const realSource = fs.realpathSync(root);
  const realStaging = resolveExistingPath(stagingRoot);
  if (!isWithin(resolved, fs.realpathSync(workspace)) ||
      isWithin(resolved, realSource) || isWithin(realSource, resolved) ||
      isWithin(resolved, realStaging) || isWithin(realStaging, resolved) ||
      (fs.existsSync(target) && fs.lstatSync(target).isSymbolicLink())) {
    throw new Error('Output path must not redirect into source, staging or outside the workspace.');
  }
  if (fs.existsSync(target) && !fs.statSync(target).isDirectory()) throw new Error('Output must be a directory.');
  return target;
}

function removeStaging(directory) {
  const absolute = path.resolve(directory);
  const realStaging = resolveExistingPath(stagingRoot);
  const resolved = resolveExistingPath(absolute);
  if (!isWithin(realStaging, fs.realpathSync(workspace)) ||
      absolute === path.resolve(stagingRoot) || !isWithin(absolute, stagingRoot) ||
      resolved === realStaging || !isWithin(resolved, realStaging)) {
    throw new Error('Refusing to remove a directory outside package staging.');
  }
  if (fs.existsSync(absolute) && fs.lstatSync(absolute).isSymbolicLink()) {
    throw new Error('Refusing to recursively remove a staging link.');
  }
  fs.rmSync(absolute, { recursive: true, force: true });
}

function publishPackage(staging, output) {
  const target = validateOutputPath(output);
  const resolvedStaging = fs.realpathSync(staging);
  const realStagingRoot = fs.realpathSync(stagingRoot);
  if (!isWithin(realStagingRoot, fs.realpathSync(workspace)) ||
      resolvedStaging === realStagingRoot || !isWithin(resolvedStaging, realStagingRoot)) {
    throw new Error('Package must be built inside this workspace staging directory.');
  }
  const backup = path.join(stagingRoot, `previous-${path.basename(target)}-${process.pid}-${Date.now()}`);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  let backedUp = false;
  if (fs.existsSync(target)) {
    fs.renameSync(target, backup);
    backedUp = true;
  }
  try { fs.renameSync(staging, target); }
  catch (error) {
    if (backedUp) fs.renameSync(backup, target);
    throw error;
  }
  if (backedUp) {
    try { removeStaging(backup); }
    catch {
      // A running old executable can keep the backup busy. Preserve it safely.
      console.warn(`Previous package retained in staging: ${backup}`);
    }
  }
  return target;
}

async function buildPackage(output, { locale = 'zh-CN' } = {}) {
  if (!['zh-CN', 'en'].includes(locale)) throw new Error('Unsupported edition language');
  const target = validateOutputPath(output);
  fs.mkdirSync(stagingRoot, { recursive: true });
  if (!isWithin(fs.realpathSync(stagingRoot), fs.realpathSync(workspace)) ||
      fs.lstatSync(stagingRoot).isSymbolicLink()) throw new Error('Invalid package staging directory.');
  const staging = fs.mkdtempSync(path.join(stagingRoot, 'tiga-portable-'));
  try {
    const electronPath = require('electron');
    copy(path.dirname(electronPath), staging);
    const exe = path.join(staging, 'TigaEnergyMonitor.exe');
    fs.renameSync(path.join(staging, 'electron.exe'), exe);
    const defaultApp = path.join(staging, 'resources', 'default_app.asar');
    if (fs.existsSync(defaultApp)) fs.unlinkSync(defaultApp);
    const destination = path.join(staging, 'resources', 'app');
    fs.mkdirSync(destination, { recursive: true });
    for (const name of ['main.cjs', 'preload.cjs', 'README.txt', 'LICENSE', 'ASSET-LICENSE.md', 'THIRD_PARTY_NOTICES.md', 'ASSET-NOTES.txt', 'integration-notes.txt']) {
      copy(path.join(root, name), path.join(destination, name));
    }
    const packageJson = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
    packageJson.appLocale = locale;
    if (locale === 'en') {
      packageJson.version = `${packageJson.version.split('-')[0]}-en`;
      packageJson.productName = 'Tiga Energy Monitor';
    }
    delete packageJson.devDependencies;
    delete packageJson.scripts;
    fs.writeFileSync(path.join(destination, 'package.json'), `${JSON.stringify(packageJson, null, 2)}\n`);
    copy(path.join(root, 'ui'), path.join(destination, 'ui'));
    copy(path.join(root, 'runtime'), path.join(destination, 'runtime'));
    copy(path.join(root, 'native', 'claude'), path.join(destination, 'native', 'claude'));
    copy(path.join(root, 'backend'), path.join(destination, 'backend'), { filter: (source) => !source.split(path.sep).includes('node_modules') });
    const dependencyRoot = path.join(root, 'backend', 'claude-runtime', 'node_modules');
    for (const dependency of ['node-pty', '@xterm/headless']) {
      copy(fs.realpathSync(path.join(dependencyRoot, dependency)), path.join(destination, 'backend', 'claude-runtime', 'node_modules', dependency));
    }
    fs.mkdirSync(path.join(staging, 'licenses'), { recursive: true });
    for (const name of ['README.txt', 'LICENSE', 'ASSET-LICENSE.md', 'THIRD_PARTY_NOTICES.md', 'ASSET-NOTES.txt']) copy(path.join(root, name), path.join(staging, name));
    if (locale === 'en') {
      const englishReadme = path.join(root, 'README.en.txt');
      copy(englishReadme, path.join(staging, 'README.txt'));
      copy(englishReadme, path.join(destination, 'README.txt'));
    }
    copy(path.join(root, 'native', 'core-helper', 'reuse-manifest.json'), path.join(staging, 'licenses', 'old-core-reuse.json'));
    const notices = path.join(root, 'licenses');
    if (fs.existsSync(notices)) copy(notices, path.join(staging, 'licenses'));
    const { rcedit } = await import('rcedit');
    const releaseVersion = packageJson.version.split('-')[0];
    if (!/^\d+\.\d+\.\d+$/.test(releaseVersion)) throw new Error('Invalid application version');
    const windowsVersion = `${releaseVersion}.0`;
    const productName = locale === 'en' ? 'Tiga Energy Monitor' : '迪迦额度显示器';
    await rcedit(exe, { icon: path.join(root, 'ui', 'assets', 'app-icon.ico'), 'file-version': windowsVersion, 'product-version': windowsVersion, 'version-string': { ProductName: productName, FileDescription: `${productName} v${packageJson.version}`, CompanyName: 'Tiga Energy Monitor', OriginalFilename: 'TigaEnergyMonitor.exe' } });
    publishPackage(staging, target);
    console.log(JSON.stringify({ executable: path.join(target, 'TigaEnergyMonitor.exe'), output: target }));
  } catch (error) {
    if (fs.existsSync(staging)) removeStaging(staging);
    throw error;
  }
}

if (require.main === module) {
  buildPackage(process.argv[2] || path.join(root, '..', 'portable-preview'), { locale: process.argv.includes('--locale=en') ? 'en' : 'zh-CN' })
    .catch((error) => { console.error(error); process.exitCode = 1; });
}

module.exports = { buildPackage, validateOutputPath, publishPackage };
