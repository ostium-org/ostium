export type House = {
  id: string;
  name: string;
  address: string;
};

// Placeholder data until the backend exists. Replace with a fetch from the API.
export const HOUSES: House[] = [
  { id: 'house-1', name: 'Maple House', address: '12 Maple Street' },
  { id: 'house-2', name: 'Harbour View', address: '48 Water Street, Unit 3' },
  { id: 'house-3', name: 'Cedar Lodge', address: '7 Cedar Crescent' },
  { id: 'house-4', name: 'Birch Court', address: '103 Birch Avenue' },
  { id: 'house-5', name: 'Willow Flats', address: '29 Willow Road, Unit 12' },
];