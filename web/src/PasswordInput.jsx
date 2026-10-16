import { useState } from 'preact/hooks';
import { EyeIcon, EyeOffIcon } from './Icons.jsx';

export function PasswordInput({ label, value, onInput, ...rest }) {
  const [shown, setShown] = useState(false);
  return (
    <label>
      {label}
      <span class="pw-wrap">
        <input type={shown ? 'text' : 'password'} value={value} onInput={(e) => onInput(e.target.value)} {...rest} />
        <button type="button" class="pw-toggle" aria-label={shown ? 'Hide password' : 'Show password'}
          aria-pressed={shown} onClick={() => setShown(!shown)}>
          {shown ? <EyeOffIcon size={20} /> : <EyeIcon size={20} />}
        </button>
      </span>
    </label>
  );
}
