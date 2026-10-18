import { randomUUID } from 'expo-crypto';
import { useIsFocused } from 'expo-router';
import { useEffect, useRef, useState } from 'react';
import { AppState } from 'react-native';

import { getCurrentDemoQRCode, remainingQRSeconds, type DemoQRCode } from '@/utils/qr-code';

export function useRotatingQRCode(houseId: string | undefined) {
  const isFocused = useIsFocused();
  const current = useRef<DemoQRCode | null>(null);
  const [code, setCode] = useState<DemoQRCode | null>(null);
  const [seconds, setSeconds] = useState(60);
  const [error, setError] = useState(false);

  // When the houseId changes, reset the QR code and start over.
  useEffect(() => {
    if (!houseId || !isFocused) return;
    const selectedHouseId = houseId;
    let timer: ReturnType<typeof setInterval> | undefined;

    function update() {
      const now = Date.now();
      try {
        const next = getCurrentDemoQRCode(current.current, selectedHouseId, now, randomUUID);
        if (next !== current.current) {
          current.current = next;
          setCode(current.current);
        }
        setSeconds(remainingQRSeconds(next.expiresAt, now));
        setError(false);
      } catch {
        current.current = null;
        setCode(null);
        setError(true);
      }
    }

    // Stop the timer when the component unmounts or the houseId changes.
    function stop() {
      if (timer !== undefined) clearInterval(timer);
      timer = undefined;
    }

    function start() {
      stop();
      update();
      timer = setInterval(update, 250);
    }

    // Start the timer when the component mounts or the houseId changes.
    if (AppState.currentState === 'active' || AppState.currentState === null) start();
    const subscription = AppState.addEventListener('change', (state) => {
      if (state === 'active') start();
      else stop();
    });
    return () => {
      stop();
      subscription.remove();
    };
  }, [houseId, isFocused]);

  // Return the current QR code, remaining time, and any error state.
  return { code: isFocused && code?.houseId === houseId ? code : null, seconds, error };
}
