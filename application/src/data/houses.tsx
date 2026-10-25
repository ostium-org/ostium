import { useSyncExternalStore } from 'react';

export type House = {
  id: string;
  name: string;
  address: string;
  building?: string;
  floor?: string;
  notes?: string;
};

export type NewHouse = {
  name: string;
  address: string;
  building: string;
  floor: string;
  notes: string;
};

const INITIAL_HOUSES: House[] = [
  { id: 'house-1', name: 'Maple House', address: '12 Maple Street' },
  { id: 'house-2', name: 'Harbour View', address: '48 Water Street, Unit 3' },
  { id: 'house-3', name: 'Cedar Lodge', address: '7 Cedar Crescent' },
  { id: 'house-4', name: 'Birch Court', address: '103 Birch Avenue' },
  { id: 'house-5', name: 'Willow Flats', address: '29 Willow Road, Unit 12' },
];

// Demo data only. Added houses disappear when the app reloads.
export let HOUSES: House[] = [...INITIAL_HOUSES];

const listeners = new Set<() => void>();
let nextHouseNumber = 1;

function subscribe(listener: () => void) {
  listeners.add(listener);

  return () => {
    listeners.delete(listener);
  };
}

function getSnapshot() {
  return HOUSES;
}

function getServerSnapshot() {
  return INITIAL_HOUSES;
}

export function useHouses() {
  return useSyncExternalStore(subscribe, getSnapshot, getServerSnapshot);
}

export function addMockHouse(values: NewHouse): House {
  const name = values.name.trim();
  const address = values.address.trim();

  if (!name || !address) {
    throw new Error('Enter a house name and address.');
  }

  const house: House = {
    id: `demo-house-${Date.now()}-${nextHouseNumber++}`,
    name,
    address,
    building: values.building.trim(),
    floor: values.floor.trim(),
    notes: values.notes.trim(),
  };

  HOUSES = [...HOUSES, house];
  listeners.forEach((listener) => listener());

  return house;
}