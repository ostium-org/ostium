import { describe, expect, mock, test } from 'bun:test';

import { QRCredentialController } from '@/services/qr-credential-controller';
import type { QRCredential } from '@/services/qr-credentials';

const credential = (houseId = 'house-1', payload = 'signed.payload', expiresAt = 61000): QRCredential =>
  ({ houseId, payload, expiresAt, isDemo: false });
function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: Error) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const settle = async () => { await Promise.resolve(); await Promise.resolve(); };

describe('QR request lifecycle', () => {
  test('loads once, preserves the exact payload, and uses the response expiry', async () => {
    const load = mock(async () => credential('house-1', '  signed.payload\n', 121000));
    const controller = new QRCredentialController(load, () => 1000);
    controller.activate('house-1');
    controller.tick();
    expect(controller.getSnapshot().loading).toBe(true);
    await settle();
    expect(load).toHaveBeenCalledTimes(1);
    expect(controller.getSnapshot()).toMatchObject({
      credential: { payload: '  signed.payload\n' }, seconds: 120, loading: false,
    });
  });

  test('manual refresh hides the old code and ignores repeated presses while loading', async () => {
    const pending = deferred<QRCredential>();
    const load = mock(async () => credential());
    const controller = new QRCredentialController(load, () => 1000);
    controller.activate('house-1');
    await settle();
    load.mockImplementation(() => pending.promise);
    const refresh = controller.refresh();
    void controller.refresh();
    expect(load).toHaveBeenCalledTimes(2);
    expect(controller.getSnapshot()).toMatchObject({ credential: null, loading: true });
    pending.resolve(credential('house-1', 'replacement', 91000));
    await refresh;
    expect(controller.getSnapshot()).toMatchObject({ credential: { payload: 'replacement' }, seconds: 90 });
  });

  test('ignores late responses after switching houses, including switching back', async () => {
    const old = deferred<QRCredential>();
    const load = mock(() => old.promise);
    const controller = new QRCredentialController(load, () => 1000);
    controller.activate('house-1');
    load.mockImplementation(async () => credential('house-2', 'house-two'));
    controller.activate('house-2');
    await settle();
    load.mockImplementation(async () => credential('house-1', 'new-house-one'));
    controller.activate('house-1');
    await settle();
    old.resolve(credential('house-1', 'stale-house-one'));
    await settle();
    expect(controller.getSnapshot().credential?.payload).toBe('new-house-one');
  });

  test('rotates once at expiry and resumes with a new code after background expiry', async () => {
    let now = 1000;
    const load = mock(async () => credential('house-1', 'first', now + 60000));
    const controller = new QRCredentialController(load, () => now);
    controller.activate('house-1');
    await settle();
    now = 61000;
    controller.tick();
    controller.tick();
    expect(controller.getSnapshot().credential).toBeNull();
    await settle();
    expect(load).toHaveBeenCalledTimes(2);
    controller.deactivate();
    now = 300000;
    controller.tick();
    expect(load).toHaveBeenCalledTimes(2);
    controller.activate('house-1');
    await settle();
    expect(load).toHaveBeenCalledTimes(3);
    expect(controller.getSnapshot().seconds).toBe(60);
  });

  test('keeps a valid credential on return without requesting another', async () => {
    const load = mock(async () => credential());
    const controller = new QRCredentialController(load, () => 1000);
    controller.activate('house-1');
    await settle();
    controller.deactivate();
    controller.activate('house-1');
    await settle();
    expect(load).toHaveBeenCalledTimes(1);
  });

  test('ignores responses after deactivation and reloads pending work on return', async () => {
    const pending = deferred<QRCredential>();
    const load = mock(() => pending.promise);
    const controller = new QRCredentialController(load, () => 1000);
    controller.activate('house-1');
    controller.deactivate();
    pending.resolve(credential());
    await settle();
    expect(controller.getSnapshot().credential).toBeNull();
    controller.activate('house-1');
    await settle();
    expect(load).toHaveBeenCalledTimes(2);
    expect(controller.getSnapshot().credential).not.toBeNull();
  });

  test('shows failures without an automatic retry loop, then supports explicit retry', async () => {
    const load = mock(async (): Promise<QRCredential> => { throw new Error('Server error.'); });
    const controller = new QRCredentialController(load, () => 1000);
    controller.activate('house-1');
    await settle();
    controller.tick();
    controller.tick();
    expect(load).toHaveBeenCalledTimes(1);
    expect(controller.getSnapshot()).toMatchObject({ credential: null, loading: false, error: 'Server error.' });
    load.mockImplementation(async () => credential());
    await controller.refresh();
    expect(controller.getSnapshot()).toMatchObject({ error: null, loading: false, credential: credential() });
  });

  test.each([credential('wrong-house'), credential('house-1', 'expired', 1000)])(
    'rejects a stale or mismatched credential from its loader', async (value) => {
      const controller = new QRCredentialController(async () => value, () => 1000);
      controller.activate('house-1');
      await settle();
      expect(controller.getSnapshot().credential).toBeNull();
      expect(controller.getSnapshot().error).not.toBeNull();
    },
  );
});
