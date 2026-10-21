import { useIsFocused } from 'expo-router';
import { useEffect, useState, useSyncExternalStore } from 'react';
import { AppState } from 'react-native';

import { QRCredentialController } from '@/services/qr-credential-controller';
import { getQRCredential } from '@/services/qr-credentials';

export function useRotatingQRCode(houseId: string | undefined) {
  const isFocused = useIsFocused();
  const [controller] = useState(() => new QRCredentialController(getQRCredential));
  const state = useSyncExternalStore(controller.subscribe, controller.getSnapshot, controller.getSnapshot);

  useEffect(() => {
    if (!houseId || !isFocused) return;
    const selectedHouseId = houseId;
    let timer: ReturnType<typeof setInterval> | undefined;
    function stop() {
      if (timer !== undefined) clearInterval(timer);
      timer = undefined;
      controller.deactivate();
    }
    function start() {
      stop();
      controller.activate(selectedHouseId);
      timer = setInterval(controller.tick, 250);
    }
    if (AppState.currentState === 'active' || AppState.currentState === null) start();
    const subscription = AppState.addEventListener('change', (status) => {
      if (status === 'active') start();
      else stop();
    });
    return () => {
      stop();
      subscription.remove();
    };
  }, [controller, houseId, isFocused]);

  const visible = isFocused && state.active && state.houseId === houseId;
  return {
    code: visible && state.seconds > 0 ? state.credential : null,
    seconds: visible ? state.seconds : 0,
    error: visible ? state.error : null,
    loading: visible ? state.loading : Boolean(houseId),
    refresh: controller.refresh,
  };
}
