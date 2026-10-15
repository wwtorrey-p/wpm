const DAY = 86400;

// Bars for the last `days` days; missing days are zero.
export function GallonsChart({ history, days = 30, target }) {
  const today = Math.floor(Date.now() / 1000 / DAY);
  const byDay = new Map(history.map((h) => [h.day, h.gallons]));
  const bars = Array.from({ length: days }, (_, i) => {
    const day = today - days + 1 + i;
    return { day, gallons: byDay.get(day) || 0 };
  });
  const max = Math.max(10, ...bars.map((b) => b.gallons), target || 0);
  const w = 600, h = 160, bw = w / days;
  const y = (v) => h - (v / max) * h;
  return (
    <svg viewBox={`0 0 ${w} ${h + 16}`} class="chart" role="img" aria-label="Gallons pumped per day">
      {bars.map((b, i) => (
        <rect key={b.day} x={i * bw + 1} y={y(b.gallons)} width={bw - 2} height={h - y(b.gallons)}>
          <title>{`${new Date(b.day * DAY * 1000).toLocaleDateString()}: ${b.gallons.toFixed(0)} gal`}</title>
        </rect>
      ))}
      {target > 0 && <line x1="0" x2={w} y1={y(target)} y2={y(target)} class="target" />}
      <text x="0" y={h + 12}>{`${days} days, max ${max.toFixed(0)} gal`}</text>
    </svg>
  );
}
