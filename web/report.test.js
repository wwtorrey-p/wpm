import { after, before, test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { mockApi } from './mock-api.js';

let server;
let base;
const auth = { Authorization: `Basic ${Buffer.from('admin:admin').toString('base64')}` };

before(async () => {
  let handler;
  mockApi().configureServer({ middlewares: { use: (_, fn) => { handler = fn; } } });
  server = createServer((req, res) => {
    req.url = req.url.slice('/api'.length);
    handler(req, res);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  base = `http://127.0.0.1:${server.address().port}/api`;
});

after(async () => {
  await new Promise((resolve, reject) => server.close((err) => err ? reject(err) : resolve()));
});

test('report requires authentication and supports viewing and downloading', async () => {
  assert.equal((await fetch(`${base}/report`)).status, 401);
  const view = await fetch(`${base}/report`, { headers: auth });
  assert.equal(view.status, 200);
  assert.match(view.headers.get('content-type'), /^text\/html/);
  assert.equal(view.headers.get('content-disposition'), null);
  const html = await view.text();
  for (const section of ['Current conditions', 'Settings', 'Network', 'Hardware and firmware',
    'Event log', 'Daily well production', 'Capacity history']) {
    assert.ok(html.includes(`<h2>${section}</h2>`), section);
  }
  assert.match(html, /<th scope="row">Last Flo update<\/th><td>\d\d\/\d\d\/\d{4}, \d\d:\d\d:\d\d [AP]M UTC<\/td>/);
  assert.match(html, /<th scope="row">Pump rate<\/th><td>5 gal\/min<\/td>/);
  assert.doesNotMatch(html, /floLastRefresh|cycleGallons|run_end/);
  const download = await fetch(`${base}/report?download=1`, { headers: auth });
  assert.match(download.headers.get('content-disposition'), /^attachment;.*water-pump-report\.html/);
  assert.match(await download.text(), /Water Pump Monitor Report/);
});

test('report includes current values and escapes markup', async () => {
  const settings = await fetch(`${base}/settings`, {
    method: 'PUT',
    headers: { ...auth, 'Content-Type': 'application/json' },
    body: JSON.stringify({ gpm: '<script>alert(1)</script>' }),
  });
  assert.equal(settings.status, 200);
  const html = await (await fetch(`${base}/report`, { headers: auth })).text();
  assert.ok(html.includes('&lt;script&gt;alert(1)&lt;/script&gt;'));
  assert.ok(!html.includes('<script>'));
});

test('all device report fields have explicit readable labels', async () => {
  const web = await readFile(new URL('../src/web.cpp', import.meta.url), 'utf8');
  const format = await readFile(new URL('../src/report_format.cpp', import.meta.url), 'utf8');
  for (const name of ['statusJson', 'settingsJson', 'networkJson', 'systemJson']) {
    const body = web.match(new RegExp(`static cJSON\\* ${name}\\([^)]*\\) \\{([\\s\\S]*?)\\n\\}`));
    assert.ok(body, name);
    for (const [, key] of body[1].matchAll(/cJSON_Add\w+ToObject\(j,\s*"(\w+)"/g)) {
      assert.match(format, new RegExp(`\\{"${key}",\\s*"[^"]+"`), `${name}: ${key}`);
    }
  }
});
