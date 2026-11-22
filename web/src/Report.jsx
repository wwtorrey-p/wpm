import { useState } from 'preact/hooks';
import { api } from './api.js';

export function Report() {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  const generate = async (download) => {
    const tab = download ? null : window.open('', '_blank');
    if (!download && !tab) {
      setError('Could not open the report. Allow pop-ups and try again.');
      return;
    }
    if (tab) tab.opener = null;
    setBusy(true);
    setError('');
    try {
      const html = await api(`/api/report${download ? '?download=1' : ''}`);
      const url = URL.createObjectURL(new Blob([html], { type: 'text/html;charset=utf-8' }));
      if (download) {
        const link = document.createElement('a');
        link.href = url;
        link.download = 'water-pump-report.html';
        document.body.appendChild(link);
        link.click();
        link.remove();
      } else {
        tab.location.replace(url);
      }
      setTimeout(() => URL.revokeObjectURL(url), 60000);
    } catch (e) {
      if (tab) tab.close();
      setError(`Could not generate report: ${e.message}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <section class="card">
      <h2>Device report</h2>
      <p>Generate a snapshot of current conditions, settings, network, hardware, event log, and retained history.</p>
      <p>The report includes network details. Treat downloaded copies as private.</p>
      <div class="row">
        <button type="button" class="primary" disabled={busy} onClick={() => generate(false)}>View report</button>
        <button type="button" disabled={busy} onClick={() => generate(true)}>Download report</button>
      </div>
      {busy && <p class="muted">Generating report...</p>}
      {error && <p class="error">{error}</p>}
    </section>
  );
}
