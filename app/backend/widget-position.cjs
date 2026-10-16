'use strict';
const fs = require('node:fs');
const path = require('node:path');

function validPosition(value) {
  return value && Number.isInteger(value.x) && Number.isInteger(value.y) &&
    Math.abs(value.x) <= 100000 && Math.abs(value.y) <= 100000;
}

class WidgetPosition {
  constructor(dataDir) { this.file = path.join(path.resolve(dataDir), 'window-position.json'); }
  read() {
    try {
      const value = JSON.parse(fs.readFileSync(this.file, 'utf8'));
      return validPosition(value) ? { x: value.x, y: value.y } : null;
    } catch { return null; }
  }
  save(value) {
    if (!validPosition(value)) return;
    const temporary = `${this.file}.${process.pid}.tmp`;
    fs.mkdirSync(path.dirname(this.file), { recursive: true });
    fs.writeFileSync(temporary, JSON.stringify({ x: value.x, y: value.y }), { encoding: 'utf8', mode: 0o600 });
    try { fs.renameSync(temporary, this.file); }
    catch (error) { try { fs.unlinkSync(temporary); } catch {} throw error; }
  }
}

module.exports = { WidgetPosition, validPosition };
