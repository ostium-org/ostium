export type House = {
  id: string;
  name: string;
  address: string;
  building?: string;
  floor?: string;
  notes?: string;
};

//kept the form fields without restoring the temporary demo house store.
export type NewHouse = {
  name: string;
  address: string;
  building: string;
  floor: string;
  notes: string;
};
