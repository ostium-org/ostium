import { useIsFocused } from 'expo-router';
import { useEffect, useMemo, useSyncExternalStore } from 'react';
import { AppState } from 'react-native';

import { QRCredentialController } from '@/services/qr-credential-controller';
import { getQRAccessStatus, getQRCredential } from '@/services/qr-credentials';
import { useAuth } from '@/hooks/auth';

export function useRotatingQRCode(houseId: string | undefined) {
  const isFocused = useIsFocused();
  const { user } = useAuth();
  //added this so credentials and scan confirmations cannot survive an account change.
  const session = useMemo(() => ({ userId: user?.id,
    controller: new QRCredentialController(getQRCredential, Date.now, getQRAccessStatus),
  }), [user?.id]);
  const { controller } = session;
  const state = useSyncExternalStore(controller.subscribe, controller.getSnapshot, controller.getSnapshot);

  useEffect(() => {
    if (!houseId || !isFocused || !user) return;
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
  }, [controller, houseId, isFocused, user]);

  const visible = session.userId === user?.id && isFocused && state.active && state.houseId === houseId;
  return {
    code: visible && state.seconds > 0 ? state.credential : null,
    seconds: visible ? state.seconds : 0,
    error: visible ? state.error : null,
    loading: visible ? state.loading : Boolean(houseId),
    refresh: controller.refresh,
    confirmation: visible ? state.confirmation : null,
    confirmationError: visible ? state.confirmationError : null,
    retryConfirmation: controller.retryConfirmation,
    returnToCode: controller.returnToCode,
  };
}
