import { useEffect, useState } from 'preact/hooks';
import { api, fmtDuration, fmtRuntime, fmtTime } from './api.js';
import { CapacityChart } from './CapacityChart.jsx';
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

// Running capacity estimate with ticks at the known low and high levels it is corrected to.
function Gauge({ gallons, max, low, high }) {
  const pct = (v) => `${Math.max(0, Math.min(100, (v / max) * 100))}%`;
  return (
    <div class="gauge">
      <div class="gauge-head">
        <span class="metric-label">Estimated capacity</span>
        <span class="metric-value">{Math.round(gallons)} / {Math.round(max)} gal</span>
      </div>
      <div class="gauge-bar" role="img" aria-label={`Capacity ${Math.round(gallons)} of ${Math.round(max)} gallons`}>
        <div class="gauge-fill" style={{ width: pct(gallons) }} />
        {low != null && <span class="gauge-tick" style={{ left: pct(low) }} title={`Low ${low} gal`} />}
        {high != null && <span class="gauge-tick" style={{ left: pct(high) }} title={`High ${high} gal`} />}
      </div>
    </div>
  );
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
  const [capHist] = usePolling(() => api('/api/capacity'), 60000);
  const [settings, setSettings] = useState(null);
  const target = settings ? settings.targetGalPerDay : 0;

  useEffect(() => { api('/api/settings').then(setSettings).catch(() => {}); }, []);

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

        {view === 'settings' && <Settings onSaved={setSettings} />}
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
                <Gauge gallons={st.capacityGal} max={st.maxCapacityGal} low={settings && settings.lowLevelGal} high={settings && settings.highLevelGal} />
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
                  <Metric label="Demand runtime" value={fmtRuntime(st.cycleRunS)} />
                  <Metric label="Average, last 30 days" value={<>{(last30 / 30).toFixed(1)} gal/day{target ? <span class="sub">(target {target})</span> : null}</>} />
                </div>
              </section>
            )}
            {!st && !err && <p class="muted">Loading...</p>}
            <section class="card">
              <h2>Level</h2>
              <p class="muted small">Estimated water level and average pump rate</p>
              <CapacityChart
                samples={capHist || []}
                history={hist || []}
                now={st ? st.epoch : 0}
                capacity={st ? st.capacityGal : 0}
                max={st ? st.maxCapacityGal : 2000}
                target={target}
              />
            </section>
          </>
        )}
      </main>
    </div>
  );
}
