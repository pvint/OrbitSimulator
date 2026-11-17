// The main trajectory canvas: Earth, the 100 km line, the launch site and any
// number of paths. Mouse wheel zooms around the cursor, dragging pans.
//
// World coordinates are metres with Earth's centre at the origin and y up.
// The camera is a world point shown at the canvas centre plus a scale in pixels per metre.

import { R_EARTH, KARMAN_LINE } from '../constants.js';

const BACKGROUND = '#070b16';
const EARTH_INNER = '#2a6fb0';
const EARTH_OUTER = '#16406e';

export class TrajectoryView {
  /** @param canvas an HTMLCanvasElement sized by CSS */
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.center = { x: 0, y: 0 };
    this.scale = 1e-4;
    /** Paths to draw, back to front. Each is
     *  {samples, events?, color, alpha?, width?, showMarkers?} */
    this.paths = [];

    this.#attachPanZoom();
    new ResizeObserver(() => this.draw()).observe(canvas);
    this.fitEarth();
  }

  setPaths(paths) {
    this.paths = paths;
    this.draw();
  }

  /** Zoom so the whole Earth fills most of the view. */
  fitEarth() {
    this.#fitBox(-R_EARTH, -R_EARTH, R_EARTH, R_EARTH);
  }

  /** Zoom to the bounding box of every path (and the launch site). */
  fitTrajectory() {
    let minX = 0, maxX = 0, minY = R_EARTH, maxY = R_EARTH;
    for (const path of this.paths) {
      for (const s of path.samples) {
        if (s.x < minX) minX = s.x;
        if (s.x > maxX) maxX = s.x;
        if (s.y < minY) minY = s.y;
        if (s.y > maxY) maxY = s.y;
      }
    }
    // Never zoom in closer than a 200 km wide box, so short hops stay readable.
    const minSize = 200e3;
    if (maxX - minX < minSize) { const c = (minX + maxX) / 2; minX = c - minSize / 2; maxX = c + minSize / 2; }
    if (maxY - minY < minSize) { const c = (minY + maxY) / 2; minY = c - minSize / 2; maxY = c + minSize / 2; }
    this.#fitBox(minX, minY, maxX, maxY);
  }

  #fitBox(minX, minY, maxX, maxY) {
    const { width, height } = this.#cssSize();
    const margin = 0.1;
    this.center = { x: (minX + maxX) / 2, y: (minY + maxY) / 2 };
    this.scale = Math.min(width / (maxX - minX), height / (maxY - minY)) * (1 - 2 * margin);
    this.draw();
  }

  #cssSize() {
    return { width: this.canvas.clientWidth || 800, height: this.canvas.clientHeight || 600 };
  }

  toScreen(x, y) {
    const { width, height } = this.#cssSize();
    return [width / 2 + (x - this.center.x) * this.scale, height / 2 - (y - this.center.y) * this.scale];
  }

  toWorld(px, py) {
    const { width, height } = this.#cssSize();
    return { x: this.center.x + (px - width / 2) / this.scale, y: this.center.y - (py - height / 2) / this.scale };
  }

  #attachPanZoom() {
    const canvas = this.canvas;
    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      // Zoom about the point under the cursor: it stays put on screen.
      const rect = canvas.getBoundingClientRect();
      const px = e.clientX - rect.left, py = e.clientY - rect.top;
      const before = this.toWorld(px, py);
      this.scale *= Math.exp(-e.deltaY * 0.0015);
      this.scale = Math.min(Math.max(this.scale, 1e-8), 1);
      const after = this.toWorld(px, py);
      this.center.x += before.x - after.x;
      this.center.y += before.y - after.y;
      this.draw();
    }, { passive: false });

    let drag = null;
    canvas.addEventListener('pointerdown', (e) => {
      drag = { x: e.clientX, y: e.clientY };
      canvas.setPointerCapture(e.pointerId);
      canvas.classList.add('dragging');
    });
    canvas.addEventListener('pointermove', (e) => {
      if (!drag) return;
      this.center.x -= (e.clientX - drag.x) / this.scale;
      this.center.y += (e.clientY - drag.y) / this.scale;
      drag = { x: e.clientX, y: e.clientY };
      this.draw();
    });
    const endDrag = () => { drag = null; canvas.classList.remove('dragging'); };
    canvas.addEventListener('pointerup', endDrag);
    canvas.addEventListener('pointercancel', endDrag);
  }

  draw() {
    const { canvas, ctx } = this;
    const { width, height } = this.#cssSize();
    // Draw at device resolution so lines stay sharp on high-DPI screens.
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
      canvas.width = Math.round(width * dpr);
      canvas.height = Math.round(height * dpr);
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    ctx.fillStyle = BACKGROUND;
    ctx.fillRect(0, 0, width, height);

    this.#drawEarth();
    for (const path of this.paths) this.#drawPath(path);
    this.#drawLaunchSite();
    this.#drawScaleBar(width, height);
  }

  #drawEarth() {
    const { ctx } = this;
    const [cx, cy] = this.toScreen(0, 0);
    const r = R_EARTH * this.scale;

    const gradient = ctx.createRadialGradient(cx - r * 0.3, cy - r * 0.3, r * 0.1, cx, cy, r);
    gradient.addColorStop(0, EARTH_INNER);
    gradient.addColorStop(1, EARTH_OUTER);
    ctx.fillStyle = gradient;
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, 2 * Math.PI);
    ctx.fill();

    // The 100 km "edge of space" line.
    ctx.strokeStyle = 'rgba(160, 200, 255, 0.35)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.arc(cx, cy, (R_EARTH + KARMAN_LINE) * this.scale, 0, 2 * Math.PI);
    ctx.stroke();
    ctx.setLineDash([]);
  }

  #drawLaunchSite() {
    const { ctx } = this;
    const [x, y] = this.toScreen(0, R_EARTH);
    ctx.fillStyle = '#ffb347';
    ctx.beginPath();
    ctx.moveTo(x, y - 9);
    ctx.lineTo(x - 5, y + 1);
    ctx.lineTo(x + 5, y + 1);
    ctx.closePath();
    ctx.fill();
  }

  /** Burn segments are drawn thicker than coasting segments. */
  #drawPath({ samples, events = [], color, alpha = 1, width = 1.5, showMarkers = false }) {
    if (samples.length < 2) return;
    const { ctx } = this;
    const coast = new Path2D();
    const burn = new Path2D();

    let prev = this.toScreen(samples[0].x, samples[0].y);
    for (let i = 1; i < samples.length; i++) {
      const p = this.toScreen(samples[i].x, samples[i].y);
      const target = samples[i - 1].burning ? burn : coast;
      target.moveTo(prev[0], prev[1]);
      target.lineTo(p[0], p[1]);
      prev = p;
    }

    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.strokeStyle = color;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.lineWidth = width;
    ctx.stroke(coast);
    ctx.lineWidth = width + 1.5;
    ctx.stroke(burn);

    if (showMarkers) {
      // Engine events as small rings.
      ctx.lineWidth = 1.5;
      for (const { sample } of events) {
        const [x, y] = this.toScreen(sample.x, sample.y);
        ctx.beginPath();
        ctx.arc(x, y, 4, 0, 2 * Math.PI);
        ctx.stroke();
      }
      // The end point: a dot.
      const last = samples[samples.length - 1];
      const [x, y] = this.toScreen(last.x, last.y);
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, 2 * Math.PI);
      ctx.fill();
    }
    ctx.restore();
  }

  /** A bar in the corner showing a round distance at the current zoom. */
  #drawScaleBar(width, height) {
    const { ctx } = this;
    const targetPx = 120;
    const metres = niceNumber(targetPx / this.scale);
    const px = metres * this.scale;
    const x = 16, y = height - 20;

    ctx.strokeStyle = 'rgba(220, 230, 255, 0.7)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x, y - 4);
    ctx.lineTo(x, y);
    ctx.lineTo(x + px, y);
    ctx.lineTo(x + px, y - 4);
    ctx.stroke();
    ctx.fillStyle = 'rgba(220, 230, 255, 0.8)';
    ctx.font = '12px system-ui, sans-serif';
    ctx.fillText(metres >= 1000 ? `${(metres / 1000).toLocaleString()} km` : `${metres} m`, x, y - 8);
  }
}

/** The largest 1, 2 or 5 × 10ⁿ that is ≤ value. */
function niceNumber(value) {
  const exp = Math.floor(Math.log10(value));
  const base = 10 ** exp;
  const f = value / base;
  return (f >= 5 ? 5 : f >= 2 ? 2 : 1) * base;
}
