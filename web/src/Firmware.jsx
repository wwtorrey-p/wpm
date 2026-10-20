import { useEffect, useState } from 'preact/hooks';
import { api, uploadFirmware } from './api.js';

const MAX_BYTES = 3 * 1024 * 1024;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function Firmware() {
  const [info, setInfo] = useState(null);
  const [file, setFile] = useState(null);
  const [phase, setPhase] = useState('idle'); // idle | uploading | restarting
  const [progress, setProgress] = useState(0);
  const [msg, setMsg] = useState('');

  useEffect(() => { api('/api/status').then(setInfo).catch(() => {}); }, []);

  const pick = (e) => {
    const f = e.target.files[0] || null;
    setMsg('');
    if (f && (!f.name.endsWith('.bin') || f.size > MAX_BYTES)) {
      setFile(null);
      setMsg('Choose a firmware .bin file (up to 3 MB)');
      return;
    }
    setFile(f);
  };

  const waitForDevice = async () => {
    await sleep(8000);
    for (let i = 0; i < 40; i++) {
      try { await api('/api/status'); return true; } catch (e) { if (e.message === 'Unauthorized') return false; }
      await sleep(2000);
    }
    return false;
  };

  const update = async () => {
    if (!file || !confirm('Install this firmware and restart the device? The pump is not controlled during the restart.')) return;
    setPhase('uploading');
    setProgress(0);
    setMsg('');
    try {
      await uploadFirmware(file, setProgress);
    } catch (e) {
      setPhase('idle');
      setMsg(e.message);
      return;
    }
    setPhase('restarting');
    const back = await waitForDevice();
    if (back) location.reload();
    else { setPhase('idle'); setMsg('The device did not come back. It rolls back to the previous firmware if the new one fails to start.'); }
  };

  return (
    <section class="card">
      <h2>Firmware update</h2>
      {info && <p class="muted small">Running {info.version}, built {info.built}</p>}
      <label>
        Firmware file (.bin)
        <input type="file" accept=".bin" onChange={pick} disabled={phase !== 'idle'} />
      </label>
      {phase === 'uploading' && <progress value={progress} max="1" class="progress" />}
      {phase === 'restarting' && <p class="muted">Installed. Restarting...</p>}
      <div class="row">
        <button type="button" class="primary" disabled={!file || phase !== 'idle'} onClick={update}>Upload and restart</button>
        <span class="error">{msg}</span>
      </div>
    </section>
  );
}
