import { useEffect, useState } from 'preact/hooks';
import { api, setCredentials } from './api.js';
import { Firmware } from './Firmware.jsx';
import { PasswordInput } from './PasswordInput.jsx';

export function Settings({ onSaved }) {
  const [s, setS] = useState(null);
  const [msg, setMsg] = useState('');
  const [pw, setPw] = useState('');
  const [pw2, setPw2] = useState('');
  const [pwMsg, setPwMsg] = useState('');

  useEffect(() => { api('/api/settings').then(setS).catch((e) => setMsg(e.message)); }, []);
  if (!s) return <p class="muted">{msg || 'Loading...'}</p>;

  const field = (key, label, step = 'any') => (
    <label>
      {label}
      <input type="number" step={step} value={s[key]} onInput={(e) => setS({ ...s, [key]: Number(e.target.value) })} />
    </label>
  );

  const save = async (e) => {
    e.preventDefault();
    try {
      const saved = await api('/api/settings', 'PUT', s);
      setS(saved);
      setMsg('Saved');
      onSaved(saved);
    } catch (err) { setMsg(err.message); }
  };

  const changePassword = async (e) => {
    e.preventDefault();
    if (pw !== pw2) { setPwMsg('Passwords do not match'); return; }
    try {
      await api('/api/password', 'POST', { password: pw });
      setCredentials('admin', pw);
      setPw('');
      setPw2('');
      setPwMsg('Password changed');
    } catch (err) { setPwMsg(err.message); }
  };

  return (
    <>
      <form onSubmit={save} class="card">
        <h2>Settings</h2>
        {field('initialBackoffH', 'Initial back-off (hours)')}
        {field('maxBackoffH', 'Maximum back-off (hours)')}
        {field('graceS', 'Restart check delay (s, min 35)', 1)}
        {field('gpm', 'Pump rate (gal/min)')}
        {field('targetGalPerDay', 'Target (gal/day)')}
        {field('lowLevelGal', 'Low level: capacity when the pump starts (gal)')}
        {field('highLevelGal', 'High level: capacity when the pump stops cleanly (gal, max 2000)')}
        <div class="row"><button type="submit" class="primary">Save</button><span class="muted">{msg}</span></div>
      </form>
      <form onSubmit={changePassword} class="card">
        <h2>Change password</h2>
        <PasswordInput label="New password" autocomplete="new-password" minLength={6} value={pw}
          onInput={(v) => { setPw(v); setPwMsg(''); }} required />
        <PasswordInput label="Confirm new password" autocomplete="new-password" minLength={6} value={pw2}
          onInput={(v) => { setPw2(v); setPwMsg(''); }} required />
        <div class="row"><button type="submit" class="primary">Change</button><span class={pwMsg === 'Password changed' ? 'muted' : 'error'}>{pwMsg}</span></div>
      </form>
      <Firmware />
    </>
  );
}
