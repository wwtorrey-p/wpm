import { api, fmtDuration } from './api.js';
import { usePolling } from './hooks.js';

const kb = (b) => `${(b / 1024).toFixed(1)} KB`;
const mb = (b) => `${(b / 1048576).toFixed(b % 1048576 ? 1 : 0)} MB`;

function Table({ title, rows }) {
  return (
    <section class="card">
      <h2>{title}</h2>
      <table>
        <tbody>
          {rows.filter(([, v]) => v != null && v !== '').map(([k, v]) => (
            <tr key={k}><th scope="row" style="text-align:left;padding-right:1em">{k}</th><td>{v}</td></tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}

export function Hardware() {
  const [s, err] = usePolling(() => api('/api/system'), 5000);
  if (!s) return <section class="card"><h2>Hardware</h2>{err && <p class="error">{err}</p>}</section>;

  return (
    <>
      <Table title="Chip" rows={[
        ['Model', `${s.chip} rev ${s.chipRevision}`],
        ['Cores', s.cores],
        ['CPU speed', `${s.cpuMHz} MHz`],
        ['Temperature', s.tempC != null ? `${s.tempC.toFixed(1)} \u00b0C (chip die)` : null],
        ['Uptime', fmtDuration(s.uptimeS)],
        ['Last reset', s.resetReason],
      ]} />
      <Table title="Memory" rows={[
        ['Free RAM', `${kb(s.heapFree)} of ${kb(s.heapTotal)}`],
        ['Lowest free since boot', kb(s.heapMinFree)],
        ['Largest free block', kb(s.heapLargestBlock)],
        ['PSRAM free', s.psramTotal ? `${mb(s.psramFree)} of ${mb(s.psramTotal)}` : null],
        ['Web server stack free', `${kb(s.httpStackFree)} (lowest)`],
      ]} />
      <Table title="Storage and firmware" rows={[
        ['Flash size', mb(s.flashSize)],
        ['Running slot', s.otaSlot ? `${s.otaSlot} (${mb(s.otaSlotSize)})` : null],
        ['Settings storage', s.nvsTotal ? `${s.nvsUsed} of ${s.nvsTotal} entries used` : null],
        ['Firmware', s.version],
        ['ESP-IDF', s.idfVersion],
      ]} />
      <Table title="Wi-Fi reliability" rows={[
        ['Disconnects since boot', s.wifiDisconnects],
        ['Last disconnect reason', s.wifiDisconnects ? s.wifiLastReason : null],
      ]} />
    </>
  );
}
