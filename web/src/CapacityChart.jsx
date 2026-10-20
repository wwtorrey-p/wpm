import { useState } from 'preact/hooks';

const DAY = 86400;
const RANGES = [7, 30, 90];
const AVG_WINDOW_DAYS = 30;

const W = 600, H = 250, L = 56, R = 66, T = 26, B = 32;
const PW = W - L - R, PH = H - T - B;
const TICKS = [0, 0.25, 0.5, 0.75, 1];

// Smallest round number that fits the data, so the right-hand scale has tidy labels.
function niceMax(v) {
  const steps = [10, 20, 50, 100, 200, 500, 1000];
  return steps.find((s) => s >= v) || Math.ceil(v / 1000) * 1000;
}

// Gallons per day averaged over up to AVG_WINDOW_DAYS ending on `day`, ignoring days before the first record.
function rollingAverage(byDay, firstDay, day) {
  const from = Math.max(firstDay, day - AVG_WINDOW_DAYS + 1);
  if (day < from) return null;
  let sum = 0;
  for (let d = from; d <= day; d++) sum += byDay.get(d) || 0;
  return sum / (day - from + 1);
}

const fmtTick = (v, max) => (max < 20 ? v.toFixed(1) : Math.round(v).toString());
const fmtDate = (t) => new Date(t * 1000).toLocaleDateString(undefined, { month: 'short', day: 'numeric' });

export function CapacityChart({ samples, history, now, capacity, max, target }) {
  const [days, setDays] = useState(30);

  const end = now || Math.floor(Date.now() / 1000);
  const start = end - days * DAY;
  const x = (t) => L + ((Math.min(Math.max(t, start), end) - start) / (end - start)) * PW;
  const yCap = (v) => T + PH - (Math.min(v, max) / max) * PH;

  // Capacity: samples in range, carried in from the last one before the range, and out to the current value.
  const inRange = samples.filter((s) => s.t >= start && s.t <= end);
  const before = [...samples].reverse().find((s) => s.t < start);
  const points = [
    ...(before ? [{ t: start, gal: before.gal }] : []),
    ...inRange,
    { t: end, gal: capacity },
  ];
  const capPath = points.map((p, i) => `${i ? 'L' : 'M'}${x(p.t).toFixed(1)},${yCap(p.gal).toFixed(1)}`).join('');

  // Average pump rate: one point per day.
  const byDay = new Map(history.map((h) => [h.day, h.gallons]));
  const firstDay = history.length ? Math.min(...history.map((h) => h.day)) : Infinity;
  const rate = [];
  for (let d = Math.floor(start / DAY); d <= Math.floor(end / DAY); d++) {
    const avg = rollingAverage(byDay, firstDay, d);
    if (avg != null) rate.push({ t: d * DAY + DAY / 2, avg });
  }
  const rateMax = niceMax(Math.max(target || 0, ...rate.map((r) => r.avg), 1));
  const yRate = (v) => T + PH - (Math.min(v, rateMax) / rateMax) * PH;
  const ratePath = rate.map((p, i) => `${i ? 'L' : 'M'}${x(p.t).toFixed(1)},${yRate(p.avg).toFixed(1)}`).join('');

  const xTicks = [0, 1 / 3, 2 / 3, 1].map((f) => start + f * (end - start));
  const last = points[points.length - 1];

  return (
    <div>
      <div class="chart-range" role="group" aria-label="Time range">
        {RANGES.map((r) => (
          <button key={r} type="button" class={r === days ? 'on' : ''} onClick={() => setDays(r)}>{r} d</button>
        ))}
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} class="chart" role="img" aria-label="Capacity and average pump rate over time">
        {TICKS.map((f) => (
          <g key={f}>
            <line class="grid" x1={L} x2={W - R} y1={T + PH - f * PH} y2={T + PH - f * PH} />
            <text class="axis-cap" x={L - 6} y={T + PH - f * PH + 5} text-anchor="end">{Math.round(f * max)}</text>
            <text class="axis-rate" x={W - R + 6} y={T + PH - f * PH + 5}>{fmtTick(f * rateMax, rateMax)}</text>
          </g>
        ))}
        {xTicks.map((t, i) => (
          <text key={i} class="axis-x" x={x(t)} y={H - 9} text-anchor={i === 0 ? 'start' : i === xTicks.length - 1 ? 'end' : 'middle'}>
            {fmtDate(t)}
          </text>
        ))}
        <text class="axis-cap" x={L - 6} y={14} text-anchor="end">gal</text>
        <text class="axis-rate" x={W - R + 6} y={14}>gal/day</text>

        {target > 0 && target <= rateMax && (
          <line class="target" x1={L} x2={W - R} y1={yRate(target)} y2={yRate(target)}>
            <title>{`Target ${target} gal/day`}</title>
          </line>
        )}
        <path class="rate-line" d={ratePath} />
        <path class="cap-line" d={capPath} />
        <circle class="cap-dot" cx={x(last.t)} cy={yCap(last.gal)} r="3.5">
          <title>{`Now ${Math.round(last.gal)} gal`}</title>
        </circle>
      </svg>
      <div class="legend">
        <span><i class="swatch cap" /> Level (gal)</span>
        <span><i class="swatch rate" /> Average (gal/day)</span>
        {target > 0 && <span><i class="swatch target" /> Target (gal/day)</span>}
      </div>
      {samples.length === 0 && <p class="muted chart-note">Capacity history builds up as the pump runs.</p>}
    </div>
  );
}
