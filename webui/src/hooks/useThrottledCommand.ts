// Command throttling for slider drags (DESIGN §8.2): at most one command
// per 100 ms while dragging (trailing edge) plus one final command on
// release; a newer value always replaces a queued one.
import { useCallback, useEffect, useLayoutEffect, useRef } from "react";

export function useThrottled<T>(send: (value: T) => void, intervalMs = 100) {
  const last = useRef(0);
  const pending = useRef<{ value: T } | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const sendRef = useRef(send);
  useLayoutEffect(() => {
    sendRef.current = send;
  }, [send]);

  useEffect(() => () => {
    if (timer.current) clearTimeout(timer.current);
  }, []);

  const flush = useCallback(() => {
    timer.current = null;
    if (pending.current) {
      const { value } = pending.current;
      pending.current = null;
      last.current = Date.now();
      sendRef.current(value);
    }
  }, []);

  const push = useCallback(
    (value: T) => {
      pending.current = { value };
      const wait = intervalMs - (Date.now() - last.current);
      if (wait <= 0) {
        if (timer.current) clearTimeout(timer.current);
        flush();
      } else if (!timer.current) {
        timer.current = setTimeout(flush, wait);
      }
    },
    [flush, intervalMs],
  );

  const commit = useCallback((value: T) => {
    if (timer.current) clearTimeout(timer.current);
    timer.current = null;
    pending.current = null;
    last.current = Date.now();
    sendRef.current(value);
  }, []);

  return { push, commit };
}
