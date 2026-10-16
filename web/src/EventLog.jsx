import { useState } from 'preact/hooks';
import { api, fmtDuration, fmtTime } from './api.js';
import { usePolling } from './hooks.js';

const LABELS = {
  boot: 'Controller started',
  demand: 'Call for water',
  run_end: 'Pump stopped',
  fault: 'Fault',
  recovery: 'Back-off ended',
  disabled: 'Disabled',
  enabled: 'Enabled',
  reset: 'Back-off reset',
};

function describe(e) {
  if (e.type === 'run_end') return `Pump stopped, ran ${fmtDuration(e.arg)}`;
  if (e.type === 'fault') return `Fault, back-off ${fmtDuration(e.arg)}`;
  return LABELS[e.type] || e.type;
}

export function EventLog() {
  const [log, err, setLog] = usePolling(() => api('/api/log'), 15000);
  const [msg, setMsg] = useState('');
  const clear = async () => {
    if (!confirm('Clear the event log? This cannot be undone.')) return;
    try { setLog(await api('/api/log', 'DELETE')); setMsg(''); } catch (e) { setMsg(e.message); }
  };
  return (
    <section class="card">
      <div class="card-head">
        <h2>Event log</h2>
        <button class="danger" onClick={clear} disabled={!log || log.length === 0}>Clear log</button>
      </div>
      {(err || msg) && <p class="error">{err || msg}</p>}
      {log && log.length === 0 && <p class="muted">No events yet.</p>}
      <table>
        <tbody>
          {(log || []).map((e, i) => (
            <tr key={i} class={e.type}>
              <td class="muted">{fmtTime(e.t)}</td>
              <td>{describe(e)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}
