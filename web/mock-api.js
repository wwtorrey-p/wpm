// Dev-only fake device API so the UI can be tried without hardware (`npm run dev`).
const now = () => Math.floor(Date.now() / 1000);

export function mockApi() {
  let password = 'admin';
  let username = 'admin';
  const st = {
    state: 'armed', enabled: true, auxClosed: true, running: false, alarm: false,
    backoffEnd: 0, curBackoffS: 0, nextBackoffS: 48 * 3600, faultCount: 3,
    totalRunS: 5 * 3600, cycleGallons: 120, cycleRunS: 1440, defaultPassword: true,
    version: '0.1.0', built: 'Oct 19 2026 09:00:00', capacityGal: 1520, maxCapacityGal: 2000,
    floStatus: 'Flo update scheduled daily', floLastRefresh: now() - 3600,
  };
  let settings = {
    initialBackoffH: 48, maxBackoffH: 720, graceS: 60, gpm: 5, targetGalPerDay: 50, lowLevelGal: 1400, highLevelGal: 1700,
  };
  const day = Math.floor(now() / 86400);
  const history = Array.from({ length: 40 }, (_, i) => ({ day: day - 39 + i, gallons: i % 9 === 0 ? 600 : 0 }))
    .filter((h) => h.gallons);
  const log = [
    { t: now() - 7200, type: 'run_end', arg: 7200, gal: 600 },
    { t: now() - 14400, type: 'demand', arg: 0 },
    { t: now() - 86400, type: 'recovery', arg: 172800 },
    { t: now() - 260000, type: 'fault', arg: 172800 },
  ];
  const full = () => ({ ...st, epoch: now() });
  const network = {
    connected: true, ip: '192.168.1.63', netmask: '255.255.255.0', gateway: '192.168.1.1', dns: '192.168.1.1',
    hostname: 'espressif', mac: 'AA:BB:CC:DD:EE:FF', ssid: 'HomeWifi', rssi: -58, channel: 6,
  };
  const system = {
    uptimeS: 93784, resetReason: 'power on', chip: 'ESP32-S3', chipRevision: 2, cores: 2, cpuMHz: 240, tempC: 41.5,
    heapFree: 182000, heapTotal: 330000, heapMinFree: 150000, heapLargestBlock: 110000, httpStackFree: 3400,
    flashSize: 8388608, otaSlot: 'ota_0', otaSlotSize: 1966080, nvsUsed: 210, nvsTotal: 756,
    version: '0.1.0', idfVersion: 'v5.4', wifiDisconnects: 2, wifiLastReason: 200,
  };

  // Sawtooth capacity: each call for water starts at the low level and climbs to the high level.
  const capacity = [];
  for (let k = 8; k >= 1; k--) {
    const t0 = now() - k * 4.5 * 86400;
    capacity.push({ t: Math.floor(t0), gal: 1700 }, { t: Math.floor(t0), gal: 1400 });
    for (let m = 1; m <= 5; m++) capacity.push({ t: Math.floor(t0 + m * 700), gal: 1400 + m * 60 });
    capacity.push({ t: Math.floor(t0 + 3600), gal: 1700 });
  }

  const actions = {
    disable: () => Object.assign(st, { enabled: false, state: 'disabled', auxClosed: false }),
    enable: () => Object.assign(st, { enabled: true, state: 'armed', auxClosed: true }),
    reset: () => Object.assign(st, { state: 'armed', auxClosed: true, backoffEnd: 0, nextBackoffS: settings.initialBackoffH * 3600 }),
    skip: () => Object.assign(st, { state: 'armed', auxClosed: true, backoffEnd: 0 }),
    // Dev-only extras to see the back-off view.
    fault: () => Object.assign(st, { state: 'backoff', auxClosed: false, curBackoffS: 172800, backoffEnd: now() + 172800, alarm: true }),
  };

  return {
    name: 'mock-api',
    configureServer(server) {
      server.middlewares.use('/api', (req, res) => {
        let body = '';
        req.setEncoding('latin1');
        req.on('data', (c) => (body += c));
        req.on('end', () => {
          const send = (obj, code = 200) => {
            res.statusCode = code;
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify(obj));
          };
          const isJson = (req.headers['content-type'] || '').includes('json');
          const json = isJson && body ? JSON.parse(body) : {};
          const expected = `Basic ${Buffer.from(`${username}:${password}`).toString('base64')}`;
          if (req.headers.authorization !== expected) return send({ error: 'unauthorized' }, 401);
          if (req.url === '/cert') { st.certIp = json.ip; return send({}); }
          if (req.url === '/firmware') return send({ ok: true });
          if (req.url.startsWith('/flo/usage')) {
            const days = Number(new URL(req.url, 'http://x').searchParams.get('days') || 7);
            const items = Array.from({ length: days }, (_, i) => ({
              time: new Date(Date.now() - (days - i) * 86400000).toISOString(),
              gallons: Math.round(Math.random() * 300) / 10,
            }));
            return send({ locationId: 'mock-location', totalGallons: items.reduce((a, b) => a + b.gallons, 0), items });
          }
          if (req.url === '/network') return send(network);
          if (req.url === '/system') return send(system);
          if (req.method === 'GET' && (req.url === '/report' || req.url === '/report?download=1')) {
            const escape = (value) => String(value).replace(/[&<>"']/g, (c) =>
              ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
            const labels = {
              state: 'Control state', enabled: 'Pump control', auxClosed: 'Float gate', running: 'Pump', alarm: 'Drive alarm relay',
              epoch: 'Device time', backoffEnd: 'Fault back-off ends', runBackoffEnd: 'Run-limit back-off ends',
              curBackoffS: 'Current fault delay', nextBackoffS: 'Next fault delay', faultCount: 'Fault count',
              totalRunS: 'Total run time', cycleGallons: 'Pumped this run', cycleRunS: 'Run time',
              capacityGal: 'Estimated capacity', maxCapacityGal: 'Maximum capacity', defaultPassword: 'Default password in use',
              floAnchor: 'Flo correction anchor', floLastRefresh: 'Last Flo update', floStatus: 'Flo update status',
              certIp: 'Certificate IP address', username: 'Username', version: 'Firmware version', built: 'Firmware build time',
              initialBackoffH: 'Initial fault delay', maxBackoffH: 'Maximum fault delay', graceS: 'Restart check delay',
              gpm: 'Pump rate', targetGalPerDay: 'Production target', lowLevelGal: 'Low capacity level',
              highLevelGal: 'High capacity level', maxRunM: 'Maximum run time', runBackoffM: 'Run-limit delay',
              waterAddGal: 'Water added per action', connected: 'Connection', ip: 'IP address', netmask: 'Subnet mask',
              gateway: 'Gateway', dns: 'DNS', hostname: 'Hostname', mac: 'MAC address', ssid: 'Wi-Fi network',
              rssi: 'Signal strength', channel: 'Wi-Fi channel', uptimeS: 'Uptime', resetReason: 'Last reset reason',
              chip: 'Chip model', chipRevision: 'Chip revision', cores: 'CPU cores', cpuMHz: 'CPU speed',
              heapFree: 'Free RAM', heapTotal: 'Total RAM', heapMinFree: 'Lowest free RAM since boot',
              heapLargestBlock: 'Largest free RAM block', psramTotal: 'Total PSRAM', psramFree: 'Free PSRAM',
              httpStackFree: 'Web server stack low-water mark', flashSize: 'Flash size', otaSlot: 'Running firmware slot',
              otaSlotSize: 'Firmware slot size', idfVersion: 'ESP-IDF version', nvsUsed: 'Settings storage entries used',
              nvsTotal: 'Settings storage entries total', tempC: 'Chip temperature', wifiDisconnects: 'Wi-Fi disconnects since boot',
              wifiLastReason: 'Last Wi-Fi disconnect reason',
            };
            const times = ['epoch', 'backoffEnd', 'runBackoffEnd', 'floAnchor', 'floLastRefresh'];
            const durations = ['curBackoffS', 'nextBackoffS', 'totalRunS', 'cycleRunS', 'uptimeS'];
            const units = {
              cycleGallons: ' gal', capacityGal: ' gal', maxCapacityGal: ' gal', lowLevelGal: ' gal',
              highLevelGal: ' gal', waterAddGal: ' gal', initialBackoffH: ' h', maxBackoffH: ' h',
              graceS: ' sec', maxRunM: ' min', runBackoffM: ' min', gpm: ' gal/min', targetGalPerDay: ' gal/day',
              rssi: ' dBm', cpuMHz: ' MHz', tempC: ' °C', heapFree: ' bytes', heapTotal: ' bytes',
              heapMinFree: ' bytes', heapLargestBlock: ' bytes', psramFree: ' bytes', psramTotal: ' bytes',
              httpStackFree: ' bytes', flashSize: ' bytes', otaSlotSize: ' bytes',
            };
            const time = (t) => {
              if (t <= 0) return 'Unavailable';
              const date = new Date(t * 1000);
              const pad = (n) => String(n).padStart(2, '0');
              const hour = date.getUTCHours();
              return `${pad(date.getUTCMonth() + 1)}/${pad(date.getUTCDate())}/${date.getUTCFullYear()}, ${pad(hour % 12 || 12)}:${pad(date.getUTCMinutes())}:${pad(date.getUTCSeconds())} ${hour < 12 ? 'AM' : 'PM'} UTC`;
            };
            const duration = (s) => `${Math.floor(s / 86400)}d ${String(Math.floor(s / 3600) % 24).padStart(2, '0')}h ${String(Math.floor(s / 60) % 60).padStart(2, '0')}m ${String(s % 60).padStart(2, '0')}s`;
            const format = (key, v) => {
              if (times.includes(key)) return time(v);
              if (durations.includes(key)) return duration(v);
              if (key === 'state') return ({ armed: 'Armed', settling: 'Settling', backoff: 'Fault back-off', run_backoff: 'Run-limit back-off', disabled: 'Disabled' })[v] || v;
              if (typeof v === 'boolean') return ({
                enabled: v ? 'Enabled' : 'Disabled', auxClosed: v ? 'Closed' : 'Open',
                running: v ? 'Running' : 'Off', alarm: v ? 'Active' : 'Inactive',
                connected: v ? 'Connected' : 'Not connected',
              })[key] || (v ? 'Yes' : 'No');
              return `${v}${units[key] || ''}`;
            };
            const section = (title, rows) => `<section><h2>${title}</h2><table>${rows.map((row) =>
              `<tr><th scope="row">${escape(row[0])}</th><td>${escape(row[1])}</td></tr>`).join('')}</table></section>`;
            const fields = (obj) => Object.entries(obj).map(([key, v]) => [labels[key] || key, format(key, v)]);
            const event = (e) => ({
              run_end: `Pump off - ran ${duration(e.arg)}, ${e.gal} gal`,
              demand: 'Pump on', fault: `Fault - back-off ${duration(e.arg)}`,
              recovery: 'Fault back-off ended',
            })[e.type] || e.type;
            const html = '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Water Pump Monitor Report</title><style>' +
              ":root{color-scheme:dark;font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}*{box-sizing:border-box}body{margin:0;color:#e8edf7;background:linear-gradient(180deg,#111c33,#0b1220 40%) fixed}header{background:#0a0f1c;border-bottom:1px solid #26365a;padding:1.1rem 1.5rem}header div,main{max-width:960px;margin:auto}header p{margin:.25rem 0 0;color:#8b9bbd;font-size:.85rem}main{padding:1.25rem}h1{font-size:1.25rem;margin:0}h2{font-size:1.05rem;margin:0 0 .75rem}section{background:#131d33;border:1px solid #26365a;border-radius:12px;padding:1.25rem;margin-bottom:1rem}p,th{color:#8b9bbd}table{border-collapse:collapse;width:100%;font-size:.92rem}th,td{text-align:left;padding:.65rem .5rem;border-bottom:1px solid #26365a;overflow-wrap:anywhere;vertical-align:top}th{font-weight:500;width:48%}tbody tr:last-child th,tbody tr:last-child td{border-bottom:0}@media(max-width:480px){main{padding:.75rem}section{padding:1rem}th,td{padding:.55rem .3rem}}@media print{:root{color-scheme:light}body,header,section{background:white;color:#172033}p,th{color:#475569}section,th,td{border-color:#cbd5e1}}" +
              '</style></head><body><header><div><h1>Water Pump Monitor</h1><p>Device report &middot; Snapshot generated by the device (mock)</p></div></header><main>' +
              '<section><h2>About this report</h2><p>Times are UTC. Pump volumes are estimates.</p></section>' +
              section('Current conditions', fields({ ...full(), username })) +
              section('Settings', fields(settings)) + section('Network', fields(network)) +
              section('Hardware and firmware', fields(system)) +
              section('Event log', log.map((e) => [time(e.t), event(e)])) +
              section('Daily well production', [...history].reverse().map((h) => [time(h.day * 86400), `${h.gallons} gal`])) +
              section('Capacity history', capacity.map((s) => [time(s.t), `${s.gal} gal`])) +
              '</main></body></html>';
            res.setHeader('Content-Type', 'text/html; charset=utf-8');
            res.setHeader('Cache-Control', 'no-store');
            if (req.url.includes('download=1')) res.setHeader('Content-Disposition', 'attachment; filename="water-pump-report.html"');
            return res.end(html);
          }
          if (req.url === '/status') return send({ ...full(), username });
          if (req.url === '/settings') {
            if (req.method === 'PUT') settings = { ...settings, ...json };
            return send(settings);
          }
          if (req.url === '/control') {
            if (!actions[json.action]) return send({}, 400);
            actions[json.action]();
            return send(full());
          }
          if (req.url === '/log') {
            if (req.method === 'DELETE') log.length = 0;
            return send(log);
          }
          if (req.url === '/history') return send(history);
          if (req.url === '/capacity') return send(capacity);
          if (req.url === '/password') {
            if (json.username) username = json.username;
            if (json.password) {
              password = json.password;
              st.defaultPassword = false;
            }
            res.end('ok');
            return;
          }
          send({}, 404);
        });
      });
    },
  };
}
