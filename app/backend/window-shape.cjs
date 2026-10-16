'use strict';
const { t } = require('./i18n.cjs');

const WIDGET_WIDTH = 420;
const WIDGET_HEIGHT = 246;
const WIDGET_MIN_WIDTH = 336;
const WIDGET_MIN_HEIGHT = 197;
const MAX_SHAPE_RECTS = 1000;
const RECT_KEYS = ['x', 'y', 'width', 'height'];
const SCALES = [80, 100, 125, 150];

function scaleFactor(scale) {
  if (!SCALES.includes(scale)) throw new TypeError(t('悬浮窗大小无效。'));
  return scale / 100;
}

function widgetDimensions(scale) {
  const factor = scaleFactor(scale);
  return { width: Math.round(WIDGET_WIDTH * factor), height: Math.ceil(WIDGET_HEIGHT * factor) };
}

function validateShapeRects(rects) {
  if (!Array.isArray(rects) || rects.length > MAX_SHAPE_RECTS) {
    throw new TypeError(t('点击区域数量无效。'));
  }
  return rects.map(rect => {
    if (!rect || typeof rect !== 'object' || Array.isArray(rect) ||
        ![Object.prototype, null].includes(Object.getPrototypeOf(rect)) ||
        Object.keys(rect).length !== RECT_KEYS.length ||
        RECT_KEYS.some(key => !Object.hasOwn(rect, key) || typeof rect[key] !== 'number' || !Number.isFinite(rect[key]))) {
      throw new TypeError(t('点击区域必须是有效矩形。'));
    }
    const { x, y, width, height } = rect;
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x + width > WIDGET_WIDTH || y + height > WIDGET_HEIGHT) {
      throw new TypeError(t('点击区域超出悬浮窗范围。'));
    }
    return { x, y, width, height };
  });
}

function scaleShapeRects(rects, scale) {
  const factor = scaleFactor(scale);
  return rects.map(({ x, y, width, height }) => {
    // Round each edge outwards so scaling never removes a silhouette pixel.
    const left = Math.floor(x * factor);
    const top = Math.floor(y * factor);
    const right = Math.ceil((x + width) * factor);
    const bottom = Math.ceil((y + height) * factor);
    return { x: left, y: top, width: right - left, height: bottom - top };
  });
}

module.exports = { WIDGET_WIDTH, WIDGET_HEIGHT, WIDGET_MIN_WIDTH, WIDGET_MIN_HEIGHT,
  MAX_SHAPE_RECTS, widgetDimensions, validateShapeRects, scaleShapeRects };
