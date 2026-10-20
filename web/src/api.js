let auth = sessionStorage.getItem('auth');
let onAuthLost = () => {};

export const setOnAuthLost = (fn) => { onAuthLost = fn; };
export const hasCredentials = () => !!auth;

const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));

export function setCredentials(user, pass) {
  auth = b64(`${user}:${pass}`);
  sessionStorage.setItem('auth', auth);
}

export function clearCredentials() {
  auth = null;
  sessionStorage.removeItem('auth');
}

export async function api(path, method = 'GET', body) {
  const headers = {};
  if (auth) headers.Authorization = `Basic ${auth}`;
  if (body) headers['Content-Type'] = 'application/json';
  const res = await fetch(path, { method, headers, body: body ? JSON.stringify(body) : undefined });
  if (res.status === 401) {
    clearCredentials();
    onAuthLost();
    throw new Error('Unauthorized');
  }
  if (!res.ok) throw new Error((await res.text()) || res.statusText);
  const type = res.headers.get('content-type') || '';
  return type.includes('json') ? res.json() : res.text();
}

export function uploadFirmware(file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/firmware');
    if (auth) xhr.setRequestHeader('Authorization', `Basic ${auth}`);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (e) => e.lengthComputable && onProgress(e.loaded / e.total);
    xhr.onload = () => {
      if (xhr.status === 401) { clearCredentials(); onAuthLost(); reject(new Error('Unauthorized')); }
      else if (xhr.status >= 200 && xhr.status < 300) resolve();
      else reject(new Error(xhr.responseText || xhr.statusText));
    };
    xhr.onerror = () => reject(new Error('Upload failed'));
    xhr.send(file);
  });
}

export function fmtRuntime(s) {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  return h ? `${h}h ${m}m ${r}s` : m ? `${m}m ${r}s` : `${r}s`;
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
