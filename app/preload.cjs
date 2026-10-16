'use strict';
const { contextBridge, ipcRenderer } = require('electron');
const invoke = (method, ...args) => ipcRenderer.invoke(`monitor:${method}`, ...args);

contextBridge.exposeInMainWorld('monitor', Object.freeze({
  locale: process.argv.includes('--tiga-locale=en') ? 'en' : 'zh-CN',
  getSnapshot: () => invoke('getSnapshot'),
  login: id => invoke('login', id),
  logout: id => invoke('logout', id),
  refresh: () => invoke('refresh'),
  updateSettings: patch => invoke('updateSettings', patch),
  updateShape: rects => invoke('updateShape', rects),
  windowAction: action => invoke('windowAction', action),
  openUsage: id => invoke('openUsage', id),
  completeClaudeSetup: () => invoke('completeClaudeSetup'),
  subscribe: callback => {
    if (typeof callback !== 'function') throw new TypeError('A callback is required.');
    const listener = (_event, snapshot) => callback(snapshot);
    ipcRenderer.on('monitor:snapshot', listener);
    return () => ipcRenderer.removeListener('monitor:snapshot', listener);
  },
}));
