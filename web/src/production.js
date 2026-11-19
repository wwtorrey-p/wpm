export const DAY = 86400;

export function productionHistory(history) {
  const byDay = new Map(history.map((h) => [h.day, h.gallons]));
  const firstDay = history.length ? Math.min(...history.map((h) => h.day)) : Infinity;
  return { byDay, firstDay };
}

export function productionAverage({ byDay, firstDay }, endDay, windowDays) {
  const from = Math.max(firstDay, endDay - windowDays + 1);
  if (endDay < from) return null;
  let gallons = 0;
  for (let day = from; day <= endDay; day++) gallons += byDay.get(day) || 0;
  const days = endDay - from + 1;
  return { gallons, days, average: gallons / days };
}
