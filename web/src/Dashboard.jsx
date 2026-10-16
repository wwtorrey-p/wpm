import { useEffect, useState } from 'preact/hooks';
import { api, fmtDuration, fmtTime } from './api.js';
import { GallonsChart } from './Chart.jsx';
import { EventLog } from './EventLog.jsx';
import { usePolling } from './hooks.js';
import {
  BackIcon, BackoffIcon, DisabledIcon, EnabledIcon, FaultIcon, MenuIcon, PumpOffIcon, PumpOnIcon,
} from './Icons.jsx';
import { Settings } from './Settings.jsx';

const MODES = {
  enabled: { label: 'Enabled', Icon: EnabledIcon },
  disabled: { label: 'Disabled', Icon: DisabledIcon },
  backoff: { label: 'Backoff', Icon: BackoffIcon },
};
const PUMPS = {
  off: { label: 'Off', Icon: PumpOffIcon },
  on: { label: 'On', Icon: PumpOnIcon },
  fault: { label: 'Fault', Icon: FaultIcon },
};

function Tile({ title, kind, def, action }) {
  const { label, Icon } = def;
  const body = (
    <>
      <span class="tile-title">{title}</span>
      <Icon size={64} />
      <span class="tile-label">{label}</span>
      {action && <span class="tile-hint">{action.hint}</span>}
    </>
  );
  return action
    ? <button type="button" class={`tile ${kind} clickable`} onClick={action.run}>{body}</button>
    : <div class={`tile ${kind}`}>{body}</div>;
}

function Metric({ label, value }) {
  return (
    <div class="metric">
      <span class="metric-label">{label}</span>
      <span class="metric-value">{value}</span>
    </div>
  );
}

export function Dashboard({ onSignOut }) {
  const [view, setView] = useState('home');
  const [menuOpen, setMenuOpen] = useState(false);
  const [notice, setNotice] = useState('');
  const [st, err, setSt] = usePolling(() => api('/api/status'), 5000);
  const [hist] = usePolling(() => api('/api/history'), 60000);
  const [target, setTarget] = useState(0);

  useEffect(() => { api('/api/settings').then((s) => setTarget(s.targetGalPerDay)).catch(() => {}); }, []);

  const go = (v) => { setMenuOpen(false); setView(v); };
  const control = async (action) => {
    setMenuOpen(false);
    try { setSt(await api('/api/control', 'POST', { action })); setNotice(''); } catch (e) { setNotice(e.message); }
  };
  const reset = () => {
    if (confirm('Reset the back-off and allow the pump to run now?')) control('reset');
    else setMenuOpen(false);
  };

  const mode = !st ? 'enabled' : st.state === 'disabled' ? 'disabled' : st.state === 'backoff' ? 'backoff' : 'enabled';
  const modeAction = {
    enabled: { hint: 'Tap to disable', run: () => control('disable') },
    disabled: { hint: 'Tap to enable', run: () => control('enable') },
    backoff: {
      hint: 'Tap to end back-off',
      run: () => { if (confirm('End the back-off now and allow the pump to run?')) control('skip'); },
    },
  }[mode];
  // The drive's alarm also fires (F26) while we hold Aux open, so it only means a fault when armed.
  const pump = !st ? 'off'
    : st.state === 'backoff' || (st.state === 'armed' && st.alarm) ? 'fault' : st.running ? 'on' : 'off';

  const today = Math.floor(Date.now() / 86400000);
  const last30 = (hist || []).filter((h) => h.day > today - 30).reduce((a, h) => a + h.gallons, 0);
  const waitLeft = st && st.state === 'backoff' && st.backoffEnd ? Math.max(0, st.backoffEnd - st.epoch) : 0;

  return (
    <div class="app">
      <header class="titlebar">
        <h1>Water Pump Monitor</h1>
        <button class="icon-btn" aria-label="Menu" aria-expanded={menuOpen} onClick={() => setMenuOpen(!menuOpen)}>
          <MenuIcon size={28} />
        </button>
        {menuOpen && (
          <>
            <div class="scrim" onClick={() => setMenuOpen(false)} />
            <nav class="menu">
              <button onClick={() => go('settings')}>Settings</button>
              <button onClick={() => go('log')}>Event log</button>
              {st && (st.enabled
                ? <button onClick={() => control('disable')}>Disable pump</button>
                : <button onClick={() => control('enable')}>Enable pump</button>)}
              <button onClick={reset}>Reset back-off</button>
              {st && st.state === 'backoff' && <button onClick={() => control('skip')}>End back-off now</button>}
              <button class="divider" onClick={onSignOut}>Sign out</button>
            </nav>
          </>
        )}
      </header>

      <main>
        {notice && <p class="banner error">{notice}</p>}
        {view !== 'home' && (
          <button class="back" onClick={() => setView('home')}><BackIcon size={20} /> Back</button>
        )}

        {view === 'settings' && <Settings onSaved={(s) => setTarget(s.targetGalPerDay)} />}
        {view === 'log' && <EventLog />}

        {view === 'home' && (
          <>
            {err && !st && <p class="banner error">{err}</p>}
            {st && st.defaultPassword && (
              <p class="banner warn">Default password in use. <a href="#" onClick={(e) => { e.preventDefault(); go('settings'); }}>Change it</a>.</p>
            )}
            {st && (
              <section class="card">
                <div class="tiles">
                  <Tile title="Control" kind={mode} def={MODES[mode]} action={modeAction} />
                  <Tile title="Pump" kind={pump} def={PUMPS[pump]} />
                </div>
                <div class="metrics">
                  {st.state === 'backoff' && (
                    <>
                      <Metric label="Back-off length" value={fmtDuration(st.curBackoffS)} />
                      <Metric label="Retry" value={st.backoffEnd
                        ? <>{fmtTime(st.backoffEnd)}<span class="sub">in {fmtDuration(waitLeft)}</span></>
                        : 'waiting for clock'} />
                    </>
                  )}
                  <Metric label="Next back-off if fault" value={fmtDuration(st.nextBackoffS)} />
                  <Metric label="Faults" value={st.faultCount} />
                  <Metric label="Total run time" value={fmtDuration(st.totalRunS)} />
                  <Metric label="Gallons this demand" value={`${st.cycleGallons.toFixed(0)} gal`} />
                  <Metric label="Average, last 30 days" value={`${(last30 / 30).toFixed(1)} gal/day${target ? ` (target ${target})` : ''}`} />
                </div>
              </section>
            )}
            {!st && !err && <p class="muted">Loading...</p>}
            <section class="card">
              <h2>Capacity</h2>
              <p class="muted small">Estimated gallons pumped per day</p>
              <GallonsChart history={hist || []} target={target} />
            </section>
          </>
        )}
      </main>
    </div>
  );
}
