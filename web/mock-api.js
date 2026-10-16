// Dev-only fake device API so the UI can be tried without hardware (`npm run dev`).
const now = () => Math.floor(Date.now() / 1000);

export function mockApi() {
  let password = 'admin';
  const st = {
    state: 'armed', enabled: true, auxClosed: true, running: false, alarm: false,
    backoffEnd: 0, curBackoffS: 0, nextBackoffS: 48 * 3600, faultCount: 3,
    totalRunS: 5 * 3600, cycleGallons: 120, defaultPassword: true,
  };
  let settings = { initialBackoffH: 48, maxBackoffH: 720, graceS: 60, gpm: 5, targetGalPerDay: 50 };
  const day = Math.floor(now() / 86400);
  const history = Array.from({ length: 40 }, (_, i) => ({ day: day - 39 + i, gallons: i % 9 === 0 ? 600 : 0 }))
    .filter((h) => h.gallons);
  const log = [
    { t: now() - 7200, type: 'run_end', arg: 7200 },
    { t: now() - 14400, type: 'demand', arg: 0 },
    { t: now() - 86400, type: 'recovery', arg: 172800 },
    { t: now() - 260000, type: 'fault', arg: 172800 },
  ];
  const full = () => ({ ...st, epoch: now() });

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
        req.on('data', (c) => (body += c));
        req.on('end', () => {
          const send = (obj, code = 200) => {
            res.statusCode = code;
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify(obj));
          };
          const json = body ? JSON.parse(body) : {};
          const expected = `Basic ${Buffer.from(`admin:${password}`).toString('base64')}`;
          if (req.headers.authorization !== expected) return send({ error: 'unauthorized' }, 401);
          if (req.url === '/status') return send(full());
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
          if (req.url === '/password') {
            password = json.password;
            st.defaultPassword = false;
            res.end('ok');
            return;
          }
          send({}, 404);
        });
      });
    },
  };
}
