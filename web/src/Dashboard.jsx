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
  run_backoff: { label: 'Run limit', Icon: BackoffIcon },
};
const PUMPS = {
  off: { label: 'Off', Icon: PumpOffIcon },
  on: { label: 'On', Icon: PumpOnIcon },
  fault: { label: 'Fault', Icon: FaultIcon },
  limit: { label: 'Run limit', Icon: FaultIcon },
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
  const [capHist, , setCapHist] = usePolling(() => api('/api/capacity'), 60000);
  const [refreshing, setRefreshing] = useState(false);
  const [refreshMsg, setRefreshMsg] = useState('');
  const [settings, setSettings] = useState(null);
  const target = settings ? settings.targetGalPerDay : 0;

  useEffect(() => { api('/api/settings').then(setSettings).catch(() => {}); }, []);

  const go = (v) => { setMenuOpen(false); setView(v); };
  const refreshCapacity = async () => {
    setRefreshing(true);
    setRefreshMsg('');
    try {
      const r = await api('/api/capacity/refresh', 'POST');
      if (r.applied) {
        setRefreshMsg(`Subtracted ${r.usedGallons} gal of water use through ${fmtTime(r.appliedThrough)}.${r.more ? ' Refresh again in an hour to catch up.' : ''}`);
        setCapHist(await api('/api/capacity'));
        setSt(await api('/api/status'));
      } else {
        setRefreshMsg(r.message);
      }
    } catch (e) { setRefreshMsg(e.message); }
    setRefreshing(false);
  };
  const control = async (action) => {
    setMenuOpen(false);
    try { setSt(await api('/api/control', 'POST', { action })); setNotice(''); } catch (e) { setNotice(e.message); }
  };
  const reset = () => {
    if (confirm('Reset the back-off and allow the pump to run now?')) control('reset');
    else setMenuOpen(false);
  };

  const mode = !st ? 'enabled' : st.state === 'disabled' ? 'disabled'
    : st.state === 'backoff' ? 'backoff' : st.state === 'run_backoff' ? 'run_backoff' : 'enabled';
  const isRunBackoff = st && st.state === 'run_backoff';
  const modeAction = {
    enabled: { hint: 'Tap to disable', run: () => control('disable') },
    disabled: { hint: 'Tap to enable', run: () => control('enable') },
    backoff: {
      hint: 'Tap to end back-off',
      run: () => { if (confirm('End the back-off now and allow the pump to run?')) control('skip'); },
    },
    run_backoff: {
      hint: 'Tap to end run-limit back-off',
      run: () => { if (confirm('End the run-limit back-off now and allow the pump to run?')) control('skip'); },
    },
  }[mode];
  // The drive's alarm also fires (F26) while we hold Aux open, so it only means a fault when armed.
  const pump = !st ? 'off'
    : isRunBackoff ? 'limit'
      : st.state === 'backoff' || (st.state === 'armed' && st.alarm) ? 'fault' : st.running ? 'on' : 'off';

  const today = Math.floor(Date.now() / 86400000);
  const last30 = (hist || []).filter((h) => h.day > today - 30).reduce((a, h) => a + h.gallons, 0);
  const waitEnd = isRunBackoff ? st.runBackoffEnd : st && st.backoffEnd;
  const waitLeft = st && (st.state === 'backoff' || isRunBackoff) && waitEnd ? Math.max(0, waitEnd - st.epoch) : 0;

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
              <button onClick={() => go('log')}>Event Log</button>
              {st && (st.enabled
                ? <button onClick={() => control('disable')}>Disable Pump</button>
                : <button onClick={() => control('enable')}>Enable Pump</button>)}
              <button onClick={reset}>Reset Fault Delay</button>
              {st && (st.state === 'backoff' || st.state === 'run_backoff') &&
                <button onClick={() => control('skip')}>End Fault Delay Now</button>}
              <button class="divider" onClick={onSignOut}>Sign Out</button>
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
                  {(st.state === 'backoff' || isRunBackoff) && (
                    <>
                      <Metric label={isRunBackoff ? 'Run Limit Delay' : 'Fault Delay'}
                        value={isRunBackoff ? fmtDuration(settings ? settings.runBackoffM * 60 : 0) : fmtDuration(st.curBackoffS)} />
                      <Metric label="Next Retry" value={waitEnd
                        ? <>{fmtTime(waitEnd)}<span class="sub">({fmtDuration(waitLeft)})</span></>
                        : 'waiting for clock'} />
                    </>
                  )}
                  <Metric label="Next Fault Delay" value={fmtDuration(st.nextBackoffS)} />
                  <Metric label="Faults" value={st.faultCount} />
                  <Metric label="Total Run Time" value={fmtDuration(st.totalRunS)} />
                  <Metric label="Pumped This Run" value={`${st.cycleGallons.toFixed(0)} gal`} />
                  <Metric label="Run Time" value={fmtRuntime(st.cycleRunS)} />
                  <Metric label="Average, Last 30 Days" value={<>{(last30 / 30).toFixed(1)} gal/day{target ? <span class="sub">(target {target})</span> : null}</>} />
                </div>
              </section>
            )}
            {!st && !err && <p class="muted">Loading...</p>}
            <section class="card">
              <h2>Cistern Water Level</h2>
              <p class="muted small">Estimated water level and average pump rate</p>
              <CapacityChart
                samples={capHist || []}
                history={hist || []}
                now={st ? st.epoch : 0}
                capacity={st ? st.capacityGal : 0}
                max={st ? st.maxCapacityGal : 2000}
                target={target}
                onRefresh={refreshCapacity}
                refreshing={refreshing}
                message={refreshMsg}
              />
            </section>
          </>
        )}
      </main>
    </div>
  );
}
