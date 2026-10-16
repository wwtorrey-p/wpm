import { useState } from 'preact/hooks';
import { api, setCredentials } from './api.js';
import { PasswordInput } from './PasswordInput.jsx';

export function Login({ onSuccess }) {
  const [user, setUser] = useState('');
  const [pass, setPass] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const submit = async (e) => {
    e.preventDefault();
    setBusy(true);
    setError('');
    setCredentials(user, pass);
    try {
      await api('/api/status');
      onSuccess();
    } catch (err) {
      setError(err.message === 'Unauthorized' ? 'Invalid username or password' : err.message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div class="login">
      <header class="titlebar"><h1>Water Pump Monitor</h1></header>
      <form class="login-body" onSubmit={submit}>
        <label>
          Username
          <input type="text" autocomplete="username" placeholder="Enter username" value={user}
            onInput={(e) => setUser(e.target.value)} autofocus required />
        </label>
        <PasswordInput label="Password" autocomplete="current-password" placeholder="Enter password"
          value={pass} onInput={setPass} required />
        {error && <p class="error" role="alert">{error}</p>}
        <button type="submit" class="primary" disabled={busy}>{busy ? 'Signing in...' : 'Sign In'}</button>
      </form>
    </div>
  );
}
