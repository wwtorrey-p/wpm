import { useEffect, useState } from 'preact/hooks';
import { clearCredentials, hasCredentials, setOnAuthLost } from './api.js';
import { Dashboard } from './Dashboard.jsx';
import { Login } from './Login.jsx';

export function App() {
  const [authed, setAuthed] = useState(hasCredentials());
  useEffect(() => { setOnAuthLost(() => setAuthed(false)); }, []);

  if (!authed) return <Login onSuccess={() => setAuthed(true)} />;
  return <Dashboard onSignOut={() => { clearCredentials(); setAuthed(false); }} />;
}
