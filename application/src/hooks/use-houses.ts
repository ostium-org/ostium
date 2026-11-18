import { useFocusEffect } from 'expo-router';
import { useCallback, useRef, useState } from 'react';

import type { House } from '@/data/houses';
import { useHouseAccessRevision } from '@/data/house-access';
import { useHouseListRevision } from '@/data/house-list';
import { useAuth } from '@/hooks/auth';
import { getHouses } from '@/services/houses';

type HouseState = { scope: string; houses: House[]; loading: boolean; error: string | null };

//added this hook so home, house details, and QR all load houses the same way.
export function useHouses() {
  const { user } = useAuth();
  const account = user ? `${user.id}:${user.role}` : '';
  const demoRevision = useHouseAccessRevision();
  const houseListRevision = useHouseListRevision();
  const [revision, setRevision] = useState(0);
  const [state, setState] = useState<HouseState>({ scope: '', houses: [], loading: false, error: null });
  const completedRequest = useRef('');
  const reload = useCallback(() => setRevision(value => value + 1), []);
  //Account and data revisions separate results; reload revision triggers an explicit fetch.
  const scope = `${account}:${demoRevision}:${houseListRevision}`;
  const request = `${scope}:${revision}`;

  useFocusEffect(useCallback(() => {
    if (!user) return;
    let active = true;
    if (completedRequest.current === request) return () => { active = false; };
    setState(current => ({
      scope,
      houses: current.scope === scope ? current.houses : [],
      loading: true,
      error: null,
    }));
    void getHouses(user).then(houses => {
      if (active) {
        completedRequest.current = request;
        setState({ scope, houses, loading: false, error: null });
      }
    }).catch(() => {
      if (active) {
        completedRequest.current = request;
        setState(current => ({
          scope,
          houses: current.scope === scope ? current.houses : [],
          loading: false,
          error: 'Could not load your houses. Please try again.',
        }));
      }
    });
    //did this so a slow request cannot update a screen after leaving it.
    return () => { active = false; };
  }, [request, scope, user]));

  const visible = Boolean(user) && scope === state.scope;
  return {
    houses: visible ? state.houses : [],
    loading: Boolean(user) && (!visible || state.loading),
    error: visible ? state.error : null,
    reload,
  };
}
