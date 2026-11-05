import { useEffect, useState } from 'preact/hooks';
import { api, setCredentials, setUsername } from './api.js';
import { Firmware } from './Firmware.jsx';
import { PasswordInput } from './PasswordInput.jsx';

export function Settings({ onSaved }) {
  const [s, setS] = useState(null);
  const [msg, setMsg] = useState('');
  const [pw, setPw] = useState('');
  const [pw2, setPw2] = useState('');
  const [pwMsg, setPwMsg] = useState('');
  const [user, setUser] = useState('');
  const [curUser, setCurUser] = useState('');
  const [floDays, setFloDays] = useState(7);
  const [flo, setFlo] = useState(null);
  const [floMsg, setFloMsg] = useState('');
  const [certIp, setCertIp] = useState(null);
  const [ip, setIp] = useState(/^\d+\.\d+\.\d+\.\d+$/.test(location.hostname) ? location.hostname : '');
  const [certMsg, setCertMsg] = useState('');

  useEffect(() => { api('/api/settings').then(setS).catch((e) => setMsg(e.message)); }, []);
  useEffect(() => {
    api('/api/status').then((st) => { setUser(st.username); setCurUser(st.username); setCertIp(st.certIp); }).catch(() => {});
  }, []);
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

  const changeAccount = async (e) => {
    e.preventDefault();
    if (pw !== pw2) { setPwMsg('Passwords do not match'); return; }
    const body = {};
    if (user !== curUser) body.username = user;
    if (pw) body.password = pw;
    if (!body.username && !body.password) { setPwMsg('Nothing to change'); return; }
    try {
      await api('/api/password', 'POST', body);
      if (body.username) setUsername(body.username);
      if (body.password) setCredentials(body.username || curUser, body.password);
      setCurUser(user);
      setPw('');
      setPw2('');
      setPwMsg('Saved');
    } catch (err) { setPwMsg(err.message); }
  };

  const makeCert = async (e) => {
    e.preventDefault();
    if (!confirm(`Create a new certificate for ${ip}? The device restarts, and you must install the new certificate again.`)) return;
    try {
      await api('/api/cert', 'POST', { ip });
      setCertMsg('Restarting with the new certificate. Reload this page in a few seconds, then download and install it.');
    } catch (err) { setCertMsg(err.message); }
  };

  const testFlo = async () => {
    setFlo(null);
    setFloMsg('Contacting Flo...');
    try {
      setFlo(await api(`/api/flo/usage?days=${floDays}&interval=1d`));
      setFloMsg('');
    } catch (err) { setFloMsg(err.message); }
  };

  return (
    <>
      <form onSubmit={save} class="card">
        <h2>Settings</h2>
        {field('initialBackoffH', 'Initial fault delay (hours)')}
        {field('maxBackoffH', 'Maximum fault delay (hours)')}
        {field('graceS', 'Restart fault check delay (s, min 35)', 1)}
        {field('gpm', 'Pump rate (gal/min)')}
        {field('targetGalPerDay', 'Target (gal/day)')}
        {field('lowLevelGal', 'Float low level (gal)')}
        {field('highLevelGal', 'Float high level (gal, max 2000)')}
        {field('waterAddGal', 'Add water amount (gal)')}
        {field('maxRunM', 'Maximum run time (minutes, 0 disables)')}
        {field('runBackoffM', 'Run delay (minutes)')}
        <div class="row"><button type="submit" class="primary">Save</button><span class="muted">{msg}</span></div>
      </form>
      <form onSubmit={changeAccount} class="card">
        <h2>Login</h2>
        <p class="muted">This login is also used to sign in to your Moen Flo account.</p>
        <label>
          Username
          <input type="text" autocomplete="username" maxLength={64} value={user}
            onInput={(e) => { setUser(e.target.value); setPwMsg(''); }} required />
        </label>
        <PasswordInput label="New password (leave blank to keep)" autocomplete="new-password" minLength={6} value={pw}
          onInput={(v) => { setPw(v); setPwMsg(''); }} />
        <PasswordInput label="Confirm new password" autocomplete="new-password" minLength={6} value={pw2}
          onInput={(v) => { setPw2(v); setPwMsg(''); }} />
        <div class="row"><button type="submit" class="primary">Save</button><span class={pwMsg === 'Saved' ? 'muted' : 'error'}>{pwMsg}</span></div>
      </form>
      <form onSubmit={makeCert} class="card">
        <h2>Certificate</h2>
        <p class="muted">
          {certIp
            ? `The certificate is valid for ${certIp}.`
            : 'The current certificate has no address, so browsers will keep warning even if it is installed.'}
          {' '}Install it as trusted on your computer to remove the warning: <a href="/cert.crt" download>download certificate</a>.
        </p>
        <label>
          Device IP address (use a fixed or reserved IP)
          <input type="text" inputMode="decimal" pattern="\d+\.\d+\.\d+\.\d+" placeholder="192.168.1.63" value={ip}
            onInput={(e) => { setIp(e.target.value); setCertMsg(''); }} required />
        </label>
        <div class="row"><button type="submit" class="primary">Create new certificate</button><span class="muted">{certMsg}</span></div>
      </form>
      <div class="card">
        <h2>Flo Usage Test</h2>
        <label>
          Days
          <input type="number" min="1" max="93" value={floDays} onInput={(e) => setFloDays(Number(e.target.value))} />
        </label>
        <div class="row"><button type="button" class="primary" onClick={testFlo}>Fetch usage</button><span class="muted">{floMsg}</span></div>
        {flo && (
          <>
            <p>Total: {flo.totalGallons} gal</p>
            <table>
              <tbody>{flo.items.map((i) => <tr key={i.time}><td>{i.time.slice(0, 10)}</td><td>{i.gallons} gal</td></tr>)}</tbody>
            </table>
          </>
        )}
      </div>
      <Firmware />
    </>
  );
}
