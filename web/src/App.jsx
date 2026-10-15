import { useEffect, useState } from 'preact/hooks';
import { api, fmtDuration, fmtTime } from './api.js';
import { GallonsChart } from './Chart.jsx';

const EVENT_LABELS = {
  boot: 'Controller started',
  demand: 'Call for water',
  run_end: 'Pump stopped (clean)',
  fault: 'Fault',
  recovery: 'Back-off ended',
  disabled: 'Disabled',
  enabled: 'Enabled',
  reset: 'Back-off reset',
};

function describe(e) {
  if (e.type === 'run_end') return `${EVENT_LABELS.run_end}, ran ${fmtDuration(e.arg)}`;
  if (e.type === 'fault') return `Fault, back-off ${fmtDuration(e.arg)}`;
  return EVENT_LABELS[e.type] || e.type;
}

function usePolling(fn, ms) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  useEffect(() => {
    let alive = true;
    const load = () =>
      fn().then((d) => alive && (setData(d), setError(null))).catch((e) => alive && setError(e.message));
    load();
    const id = setInterval(load, ms);
    return () => { alive = false; clearInterval(id); };
  }, []);
  return [data, error, setData];
}

function Settings({ onSaved }) {
  const [s, setS] = useState(null);
  const [msg, setMsg] = useState('');
  useEffect(() => { api('/api/settings').then(setS); }, []);
  if (!s) return null;
  const field = (key, label, step = 'any') => (
    <label>
      {label}
      <input type="number" step={step} value={s[key]} onInput={(e) => setS({ ...s, [key]: Number(e.target.value) })} />
    </label>
  );
  const save = async (e) => {
    e.preventDefault();
    try { setS(await api('/api/settings', 'PUT', s)); setMsg('Saved'); onSaved(); } catch (err) { setMsg(err.message); }
  };
  return (
    <form onSubmit={save} class="card">
      <h2>Settings</h2>
      {field('initialBackoffH', 'Initial back-off (hours)')}
      {field('maxBackoffH', 'Maximum back-off (hours)')}
      {field('graceS', 'Restart grace (s, min 35)', 1)}
      {field('gpm', 'Pump rate (gal/min)')}
      {field('targetGalPerDay', 'Target (gal/day)')}
      <button type="submit">Save</button> <span>{msg}</span>
    </form>
  );
}

function Password({ onDone }) {
  const [pw, setPw] = useState('');
  const [msg, setMsg] = useState('');
  const save = async (e) => {
    e.preventDefault();
    try { await api('/api/password', 'POST', { password: pw }); setMsg('Changed. Sign in again.'); setPw(''); onDone(); }
    catch (err) { setMsg(err.message); }
  };
  return (
    <form onSubmit={save} class="card">
      <h2>Change password</h2>
      <label>New password
        <input type="password" value={pw} minLength={6} onInput={(e) => setPw(e.target.value)} />
      </label>
      <button type="submit">Change</button> <span>{msg}</span>
    </form>
  );
}

export function App() {
  const [st, err, setSt] = usePolling(() => api('/api/status'), 5000);
  const [log, , ] = usePolling(() => api('/api/log'), 15000);
  const [hist] = usePolling(() => api('/api/history'), 60000);
  const [target, setTarget] = useState(0);
  useEffect(() => { api('/api/settings').then((s) => setTarget(s.targetGalPerDay)); }, []);

  const control = (action) => api('/api/control', 'POST', { action }).then(setSt);

  if (!st) return <main><p>{err || 'Loading...'}</p></main>;

  const today = Math.floor(Date.now() / 86400000);
  const last30 = (hist || []).filter((h) => h.day > today - 30).reduce((a, h) => a + h.gallons, 0);
  const waitLeft = st.state === 'backoff' && st.backoffEnd ? Math.max(0, st.backoffEnd - st.epoch) : 0;

  return (
    <main>
      <h1>Water Pump Monitor</h1>
      {err && <p class="warn">Connection problem: {err}</p>}
      {st.defaultPassword && <p class="warn">Default password in use. Change it below.</p>}
      <section class="card">
        <h2>Status: <span class={`state ${st.state}`}>{st.state}</span></h2>
        <dl>
          <dt>Pump</dt><dd>{st.running ? 'Running' : 'Idle'}</dd>
          <dt>Drive alarm</dt><dd>{st.alarm ? 'ACTIVE' : 'None'}</dd>
          <dt>Run permitted</dt><dd>{st.auxClosed ? 'Yes' : 'No'}</dd>
          {st.state === 'backoff' && (
            <>
              <dt>Back-off</dt><dd>{fmtDuration(st.curBackoffS)}</dd>
              <dt>Retry at</dt><dd>{st.backoffEnd ? `${fmtTime(st.backoffEnd)} (in ${fmtDuration(waitLeft)})` : 'waiting for clock'}</dd>
            </>
          )}
          <dt>Next back-off if fault</dt><dd>{fmtDuration(st.nextBackoffS)}</dd>
          <dt>Faults</dt><dd>{st.faultCount}</dd>
          <dt>Total run time</dt><dd>{fmtDuration(st.totalRunS)}</dd>
          <dt>Gallons since last clean stop</dt><dd>{st.cycleGallons.toFixed(0)}</dd>
          <dt>Average, last 30 days</dt><dd>{(last30 / 30).toFixed(1)} gal/day{target ? ` (target ${target})` : ''}</dd>
        </dl>
        <div class="buttons">
          {st.enabled
            ? <button onClick={() => control('disable')}>Disable pump</button>
            : <button onClick={() => control('enable')}>Enable pump</button>}
          <button onClick={() => control('reset')}>Reset back-off</button>
          {st.state === 'backoff' && <button onClick={() => control('skip')}>End back-off now</button>}
        </div>
      </section>

      <section class="card">
        <h2>Gallons per day (estimated)</h2>
        <GallonsChart history={hist || []} target={target} />
      </section>

      <section class="card">
        <h2>Event log</h2>
        <table>
          <tbody>
            {(log || []).map((e, i) => (
              <tr key={i} class={e.type}><td>{fmtTime(e.t)}</td><td>{describe(e)}</td></tr>
            ))}
          </tbody>
        </table>
      </section>

      <Settings onSaved={() => api('/api/settings').then((s) => setTarget(s.targetGalPerDay))} />
      <Password onDone={() => {}} />
    </main>
  );
}
