export async function api(path, method = 'GET', body) {
  const res = await fetch(path, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) throw new Error((await res.text()) || res.statusText);
  const type = res.headers.get('content-type') || '';
  return type.includes('json') ? res.json() : res.text();
}

export function fmtDuration(s) {
  if (!s) return '0m';
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  return [d && `${d}d`, (d || h) && `${h}h`, `${m}m`].filter(Boolean).join(' ');
}

export function fmtTime(epoch) {
  return epoch ? new Date(epoch * 1000).toLocaleString() : 'unknown';
}
