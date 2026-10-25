import { useFocusEffect } from 'expo-router';
import { useCallback, useState } from 'react';

import type { House } from '@/data/houses';
import { useHouseAccessRevision } from '@/data/house-access';
import { useAuth } from '@/hooks/auth';
import { getHouses } from '@/services/houses';

type HouseState = { request: string; houses: House[]; loading: boolean; error: string | null };

//added this hook so home, house details, and QR all load houses the same way.
export function useHouses() {
  const { user } = useAuth();
  const account = user ? `${user.id}:${user.role}` : '';
  const demoRevision = useHouseAccessRevision();
  const [revision, setRevision] = useState(0);
  const [state, setState] = useState<HouseState>({ request: '', houses: [], loading: false, error: null });
  const reload = useCallback(() => setRevision(value => value + 1), []);
  //included the account and reload number so old results are not shown after a change.
  const request = `${account}:${revision}:${demoRevision}`;

  //reload when coming back to the screen so access changes can show up.
  useFocusEffect(useCallback(() => {
    if (!user) return;
    let active = true;
    setState({ request, houses: [], loading: true, error: null });
    void getHouses(user).then(houses => {
      if (active) setState({ request, houses, loading: false, error: null });
    }).catch(() => {
      if (active) setState({ request, houses: [], loading: false, error: 'Could not load your houses. Please try again.' });
    });
    //did this so a slow request cannot update a screen after leaving it.
    return () => { active = false; };
  }, [request, user]));

  const visible = Boolean(user) && request === state.request;
  return {
    houses: visible ? state.houses : [],
    loading: Boolean(user) && (!visible || state.loading),
    error: visible ? state.error : null,
    reload,
  };
}
