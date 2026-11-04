import { api } from './api.js';
import { usePolling } from './hooks.js';

export function Network() {
  const [n, err] = usePolling(() => api('/api/network'), 5000);

  const rows = n && [
    ['Status', n.connected ? 'Connected' : 'Not connected'],
    ['Wi-Fi network', n.ssid],
    ['Signal', n.rssi != null ? `${n.rssi} dBm` : null],
    ['Channel', n.channel],
    ['IP address', n.ip],
    ['Subnet mask', n.netmask],
    ['Gateway', n.gateway],
    ['DNS', n.dns],
    ['Hostname', n.hostname],
    ['MAC address', n.mac],
  ].filter(([, v]) => v != null && v !== '');

  return (
    <section class="card">
      <h2>Network</h2>
      {err && !n && <p class="error">{err}</p>}
      {rows && (
        <table>
          <tbody>
            {rows.map(([k, v]) => <tr key={k}><th scope="row" style="text-align:left;padding-right:1em">{k}</th><td>{v}</td></tr>)}
          </tbody>
        </table>
      )}
    </section>
  );
}
