import { useEffect, useState } from 'preact/hooks';

export function usePolling(fn, ms) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  useEffect(() => {
    let alive = true;
    const load = () =>
      fn()
        .then((d) => alive && (setData(d), setError(null)))
        .catch((e) => alive && setError(e.message));
    load();
    const id = setInterval(load, ms);
    return () => { alive = false; clearInterval(id); };
  }, []);
  return [data, error, setData];
}
