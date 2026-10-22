export interface StringStorage {
  getItem(key: string): Promise<string | null>;
  setItem(key: string, value: string): Promise<void>;
  removeItem(key: string): Promise<void>;
}

type Manifest = { generation: string; count: number };

/** Atomic manifest switch keeps the previous session intact if a chunk write fails. */
export function createChunkedStorage(storage: StringStorage): StringStorage {
  let pending: Promise<unknown> = Promise.resolve();
  function serial<T>(operation: () => Promise<T>): Promise<T> {
    const next = pending.then(operation);
    pending = next.catch(() => {});
    return next;
  }
  async function manifest(key: string): Promise<Manifest | null> {
    const raw = await storage.getItem(`${key}.manifest`);
    if (!raw) return null;
    try {
      const value = JSON.parse(raw);
      return typeof value.generation === 'string' && /^[a-z0-9-]+$/.test(value.generation) &&
        Number.isInteger(value.count) && value.count > 0 && value.count <= 4096 ? value : null;
    } catch { return null; }
  }
  async function cleanup(key: string, value: Manifest | null) {
    if (!value) return;
    await Promise.all(Array.from({ length: value.count }, (_, i) => storage.removeItem(`${key}.${value.generation}.${i}`).catch(() => {})));
  }
  return {
    getItem: (key) => serial(async () => {
      const value = await manifest(key);
      if (!value) return null;
      const chunks = await Promise.all(Array.from({ length: value.count }, (_, i) => storage.getItem(`${key}.${value.generation}.${i}`)));
      if (chunks.some((chunk) => chunk === null)) return null;
      try {
        const result = JSON.parse(chunks.join(''));
        return typeof result === 'string' ? result : null;
      } catch { return null; }
    }),
    setItem: (key, text) => serial(async () => {
      // ASCII escaping bounds UTF-8 bytes as well as string length, including Unicode metadata.
      const encoded = JSON.stringify(text).replace(/[^\x20-\x7e]/g, (char) => `\\u${char.charCodeAt(0).toString(16).padStart(4, '0')}`);
      const chunks = encoded.match(/.{1,1700}/g)!;
      if (chunks.length > 4096) throw new Error('Session exceeds secure storage capacity.');
      const previous = await manifest(key);
      const next = { generation: `${Date.now()}-${Math.random().toString(36).slice(2)}`, count: chunks.length };
      try {
        for (let i = 0; i < chunks.length; i++) await storage.setItem(`${key}.${next.generation}.${i}`, chunks[i]);
        await storage.setItem(`${key}.manifest`, JSON.stringify(next));
      } catch (error) {
        await cleanup(key, next);
        throw error;
      }
      await cleanup(key, previous);
    }),
    removeItem: (key) => serial(async () => {
      const previous = await manifest(key);
      await storage.removeItem(`${key}.manifest`);
      await cleanup(key, previous);
    }),
  };
}
