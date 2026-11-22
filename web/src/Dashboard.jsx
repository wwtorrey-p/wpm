import { useEffect, useState } from 'preact/hooks';
import { api, fmtDuration, fmtRuntime, fmtTime } from './api.js';
import { CapacityChart } from './CapacityChart.jsx';
import { EventLog } from './EventLog.jsx';
import { usePolling } from './hooks.js';
import {
  BackIcon, BackoffIcon, DisabledIcon, EnabledIcon, FaultIcon, MenuIcon, PumpOffIcon, PumpOnIcon,
} from './Icons.jsx';
import { Hardware } from './Hardware.jsx';
import { Network } from './Network.jsx';
import { DAY, productionAverage, productionHistory } from './production.js';
import { Report } from './Report.jsx';
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
  const [waterNotice, setWaterNotice] = useState('');
  const [st, err, setSt] = usePolling(() => api('/api/status'), 5000);
  const [hist, histErr] = usePolling(() => api('/api/history'), 60000);
  const [capHist, , setCapHist] = usePolling(() => api('/api/capacity'), 60000);
  const [settings, setSettings] = useState(null);
  const [chartDays, setChartDays] = useState(7);
  const target = settings ? settings.targetGalPerDay : 0;

  useEffect(() => { api('/api/settings').then(setSettings).catch(() => {}); }, []);

  const go = (v) => { setMenuOpen(false); setView(v); };
  const control = async (action) => {
    setMenuOpen(false);
    try { setSt(await api('/api/control', 'POST', { action })); setNotice(''); } catch (e) { setNotice(e.message); }
  };
  const addWater = async () => {
    setMenuOpen(false);
    let amount = settings && settings.waterAddGal;
    if (amount == null) {
      try {
        const currentSettings = await api('/api/settings');
        setSettings(currentSettings);
        amount = currentSettings.waterAddGal;
      } catch (e) { setWaterNotice(e.message); return; }
    }
    if (!confirm(`Add ${amount} gal of water to the estimated capacity?`)) return;
    setWaterNotice('');
    try {
      const result = await api('/api/capacity/add', 'POST');
      setWaterNotice(`Added ${result.addedGallons} gal; estimated capacity is now ${result.capacityGal} gal.`);
      setSt((current) => current ? { ...current, capacityGal: result.capacityGal } : current);
      try { setCapHist(await api('/api/capacity')); }
      catch (e) { setWaterNotice(`Added ${result.addedGallons} gal; could not refresh capacity history: ${e.message}`); }
    } catch (e) { setWaterNotice(e.message); }
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
      : st.state === 'armed' && st.alarm ? 'fault' : st.running ? 'on' : 'off';

  const today = st && st.epoch > 0 ? Math.floor(st.epoch / DAY) : null;
  const production = productionHistory(hist || []);
  const chartProduction = today != null && hist ? productionAverage(production, today - 1, chartDays) : null;
  const floStale = st && st.floLastRefresh && st.epoch - st.floLastRefresh > 2 * DAY;
  const avgValue = (result, window) => result
    ? <>{result.average.toFixed(1)} gal/day{result.days < window && <span class="sub">({result.days} days since first record)</span>}</>
    : 'Unavailable';
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
              <button onClick={addWater}>Add Water</button>
              <button onClick={() => go('settings')}>Settings</button>
              <button onClick={() => go('network')}>Network</button>
              <button onClick={() => go('hardware')}>Hardware</button>
              <button onClick={() => go('log')}>Event Log</button>
              <button onClick={() => go('report')}>Device Report</button>
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
        {waterNotice && <p class="banner">{waterNotice}</p>}
        {view !== 'home' && (
          <button class="back" onClick={() => setView('home')}><BackIcon size={20} /> Back</button>
        )}

        {view === 'settings' && <Settings onSaved={setSettings} />}
        {view === 'network' && <Network />}
        {view === 'hardware' && <Hardware />}
        {view === 'log' && <EventLog />}
        {view === 'report' && <Report />}

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
                      <Metric label="Next Retry" value={waitEnd
                        ? <>{fmtTime(waitEnd)}<span class="sub">({fmtDuration(waitLeft)})</span></>
                        : 'waiting for clock'} />
                    </>
                  )}
                  <Metric label="Pumped This Run" value={`${st.cycleGallons.toFixed(0)} gal`} />
                  <Metric label="Run Time" value={fmtRuntime(st.cycleRunS)} />
                  {settings && Number.isFinite(st.totalRunS) && Number.isFinite(settings.gpm) &&
                    <Metric label="Total Pumped" value={`${(st.totalRunS * settings.gpm / 60).toFixed(0)} gal`} />}
                  <Metric label="Total Run Time" value={fmtDuration(st.totalRunS)} />
                  <Metric label={`Well Production, ${chartDays} Days`} value={avgValue(chartProduction, chartDays)} />
                </div>
                {histErr && <p class="error">Well production history: {histErr}</p>}
              </section>
            )}
            {!st && !err && <p class="muted">Loading...</p>}
            <section class="card">
              <h2>Cistern Water Level and Well Production</h2>
              <CapacityChart
                samples={capHist || []}
                history={hist || []}
                now={st ? st.epoch : 0}
                capacity={st ? st.capacityGal : 0}
                max={st ? st.maxCapacityGal : 2000}
                target={target}
                days={chartDays}
                onDaysChange={setChartDays}
              />
              <p style={{marginTop:'1rem'}} class={'small'}>Consumption Update</p>
              {st && <p class={`${floStale || (st.floStatus && (st.floStatus.includes('failed') || st.floStatus.includes('waiting for Wi-Fi'))) ? 'error' : 'muted'} small`}> {st.floStatus || 'Automatic update status unavailable'}.
              </p>}
              <p class={'muted small'}>Last correction: {st && st.floLastRefresh ? fmtTime(st.floLastRefresh) : 'not yet'}.
                {floStale && ' Flo correction is overdue.'}
              </p>
            </section>
          </>
        )}
      </main>
    </div>
  );
}

