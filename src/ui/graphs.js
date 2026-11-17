// Small canvas line charts of one quantity against time.

const AXIS_COLOR = 'rgba(200, 210, 230, 0.35)';
const LABEL_COLOR = 'rgba(200, 210, 230, 0.75)';
const PAD = { left: 48, right: 10, top: 22, bottom: 22 };

/**
 * @param canvas  HTMLCanvasElement sized by CSS
 * @param chart   {
 *   title, unit,
 *   series: [{ xs, ys, color, width?, alpha? }],  // xs in seconds
 *   xMax?,          // clip the time axis here (default: longest series)
 *   markers?: [t],  // times to mark with faint vertical lines
 * }
 */
export function drawChart(canvas, { title, unit, series, xMax, markers = [] }) {
  const ctx = canvas.getContext('2d');
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  // Data ranges: time from 0 to xMax, values from 0 to the largest visible value.
  if (xMax === undefined) {
    xMax = Math.max(1, ...series.map((s) => s.xs[s.xs.length - 1] ?? 0));
  }
  let yMax = 0;
  for (const s of series) {
    for (let i = 0; i < s.xs.length && s.xs[i] <= xMax; i++) yMax = Math.max(yMax, s.ys[i]);
  }
  if (!(yMax > 0)) yMax = 1;

  // Show long flights in minutes.
  const useMinutes = xMax > 1200;
  const xUnit = useMinutes ? 60 : 1;
  const xTicks = niceTicks(xMax / xUnit);
  const yTicks = niceTicks(yMax);
  const xTop = xTicks[xTicks.length - 1] * xUnit;
  const yTop = yTicks[yTicks.length - 1];

  const plotW = width - PAD.left - PAD.right;
  const plotH = height - PAD.top - PAD.bottom;
  const sx = (x) => PAD.left + (x / xTop) * plotW;
  const sy = (y) => PAD.top + plotH - (y / yTop) * plotH;

  // Grid and tick labels.
  ctx.font = '10px system-ui, sans-serif';
  ctx.lineWidth = 1;
  ctx.strokeStyle = AXIS_COLOR;
  ctx.fillStyle = LABEL_COLOR;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (const y of yTicks) {
    ctx.globalAlpha = y === 0 ? 1 : 0.4;
    line(ctx, PAD.left, sy(y), PAD.left + plotW, sy(y));
    ctx.globalAlpha = 1;
    ctx.fillText(formatTick(y), PAD.left - 5, sy(y));
  }
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (const x of xTicks) {
    ctx.globalAlpha = x === 0 ? 1 : 0.4;
    line(ctx, sx(x * xUnit), PAD.top, sx(x * xUnit), PAD.top + plotH);
    ctx.globalAlpha = 1;
    ctx.fillText(formatTick(x), sx(x * xUnit), PAD.top + plotH + 4);
  }
  // Title, with the time unit on the right.
  ctx.textBaseline = 'top';
  ctx.font = '11px system-ui, sans-serif';
  ctx.textAlign = 'left';
  ctx.fillText(`${title} (${unit})`, PAD.left, 5);
  ctx.textAlign = 'right';
  ctx.fillText(`time (${useMinutes ? 'min' : 's'})`, width - PAD.right, 5);

  // Event markers.
  ctx.save();
  ctx.setLineDash([3, 3]);
  ctx.strokeStyle = 'rgba(255, 180, 70, 0.5)';
  for (const t of markers) if (t <= xTop) line(ctx, sx(t), PAD.top, sx(t), PAD.top + plotH);
  ctx.restore();

  // Data, clipped to the plot area.
  ctx.save();
  ctx.beginPath();
  ctx.rect(PAD.left, PAD.top, plotW, plotH);
  ctx.clip();
  ctx.lineJoin = 'round';
  for (const s of series) {
    ctx.globalAlpha = s.alpha ?? 1;
    ctx.strokeStyle = s.color;
    ctx.lineWidth = s.width ?? 1.5;
    ctx.beginPath();
    for (let i = 0; i < s.xs.length; i++) {
      const px = sx(s.xs[i]), py = sy(s.ys[i]);
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
      if (s.xs[i] > xTop) break;
    }
    ctx.stroke();
  }
  ctx.restore();
}

function line(ctx, x0, y0, x1, y1) {
  ctx.beginPath();
  ctx.moveTo(x0, y0);
  ctx.lineTo(x1, y1);
  ctx.stroke();
}

/** Round tick values from 0 up to at least max, about 4–6 of them. */
function niceTicks(max) {
  const rough = max / 4;
  const exp = Math.floor(Math.log10(rough));
  const f = rough / 10 ** exp;
  const step = (f > 5 ? 10 : f > 2 ? 5 : f > 1 ? 2 : 1) * 10 ** exp;
  const ticks = [];
  for (let v = 0; v < max + step * 0.999; v += step) ticks.push(+v.toPrecision(12));
  return ticks;
}

function formatTick(v) {
  if (v === 0) return '0';
  if (Math.abs(v) >= 1e4) return `${+(v / 1000).toPrecision(3)}k`;
  return `${+v.toPrecision(4)}`;
}
