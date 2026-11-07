import { describe, expect, vi, test } from 'vitest';

import { QRCredentialController } from '@/services/qr-credential-controller';
import type { QRCredential } from '@/services/qr-credentials';

const credential = (houseId = 'house-1', payload = 'signed.payload', expiresAt = 61000): QRCredential =>
  ({ id: payload, houseId, payload, expiresAt, isDemo: false });
function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: Error) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const settle = async () => { await Promise.resolve(); await Promise.resolve(); };

describe('QR request lifecycle', () => {
  test('loads once, preserves the exact payload, and uses the response expiry', async () => {
    const load = vi.fn(async () => credential('house-1', '  signed.payload\n', 121000));
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
    const load = vi.fn(async () => credential());
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
    const load = vi.fn(() => old.promise);
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
    const load = vi.fn(async () => credential('house-1', 'first', now + 60000));
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
    const load = vi.fn(async () => credential());
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
    const load = vi.fn(() => pending.promise);
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
    const load = vi.fn(async (): Promise<QRCredential> => { throw new Error('Server error.'); });
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

const confirmation = (credentialId = 'signed.payload', houseId = 'house-1') =>
  ({ id: 'scan-1', credentialId, houseId, occurredAt: 1000 });

describe('confirmed QR access', () => {
  test('polls every two seconds, pauses on success, and returns to a fresh code', async () => {
    let now = 1000;
    const load = vi.fn(async () => credential());
    const status = vi.fn(async () => null as ReturnType<typeof confirmation> | null);
    const controller = new QRCredentialController(load, () => now, status);
    controller.activate('house-1');
    await settle();
    controller.tick();
    await settle();
    controller.tick();
    expect(status).toHaveBeenCalledTimes(1);
    status.mockResolvedValue(confirmation());
    now += 2000;
    controller.tick();
    await settle();
    expect(controller.getSnapshot()).toMatchObject({ confirmation: confirmation(), credential: null });
    now += 60000;
    controller.tick();
    expect(load).toHaveBeenCalledTimes(1);
    controller.returnToCode();
    await settle();
    expect(load).toHaveBeenCalledTimes(2);
    expect(controller.getSnapshot().confirmation).toBeNull();
  });

  test('keeps replaced codes for fifteen seconds to confirm delayed scans', async () => {
    let now = 1000;
    const status = vi.fn(async (houseId: string, id: string) => id === 'first' ? confirmation(id, houseId) : null);
    const load = vi.fn(async () => credential('house-1', 'first'));
    const controller = new QRCredentialController(load, () => now, status);
    controller.activate('house-1');
    await settle();
    load.mockResolvedValue(credential('house-1', 'second', 90000));
    await controller.refresh();
    now += 14999;
    controller.tick();
    await settle();
    expect(controller.getSnapshot().confirmation?.credentialId).toBe('first');
  });

  test('forgets replaced codes after the grace period', async () => {
    let now = 1000;
    const status = vi.fn(async () => null);
    const load = vi.fn(async () => credential('house-1', 'first'));
    const controller = new QRCredentialController(load, () => now, status);
    controller.activate('house-1');
    await settle();
    load.mockResolvedValue(credential('house-1', 'second', 90000));
    await controller.refresh();
    now += 15000;
    controller.tick();
    await settle();
    expect(status.mock.calls).toEqual([['house-1', 'second']]);
  });

  test.each(['switch', 'deactivate'] as const)('ignores pending confirmations after %s and never overlaps polls', async (change) => {
    const pending = deferred<ReturnType<typeof confirmation> | null>();
    const status = vi.fn(() => pending.promise);
    const controller = new QRCredentialController(async house => credential(house), () => 1000, status);
    controller.activate('house-1');
    await settle();
    controller.tick();
    controller.retryConfirmation();
    expect(status).toHaveBeenCalledTimes(1);
    if (change === 'switch') controller.activate('house-2');
    else controller.deactivate();
    pending.resolve(confirmation());
    await settle();
    expect(controller.getSnapshot().confirmation).toBeNull();
    expect(controller.getSnapshot().confirmationError).toBeNull();
  });

  test('status failures leave the code usable and explicit retry can recover', async () => {
    const status = vi.fn(async (): Promise<ReturnType<typeof confirmation> | null> => { throw new Error('offline'); });
    const controller = new QRCredentialController(async () => credential(), () => 1000, status);
    controller.activate('house-1');
    await settle();
    controller.tick();
    await settle();
    expect(controller.getSnapshot().credential).not.toBeNull();
    expect(controller.getSnapshot().confirmation).toBeNull();
    expect(controller.getSnapshot().confirmationError).toContain('unavailable');
    status.mockResolvedValue(confirmation());
    controller.retryConfirmation();
    await settle();
    expect(controller.getSnapshot().confirmation).toEqual(confirmation());
  });

  test('demo codes never request or show confirmed access', async () => {
    const status = vi.fn(async () => confirmation());
    const controller = new QRCredentialController(async () => ({ ...credential(), isDemo: true }), () => 1000, status);
    controller.activate('house-1');
    await settle();
    controller.tick();
    expect(status).not.toHaveBeenCalled();
    expect(controller.getSnapshot().confirmation).toBeNull();
  });

  test('leaving success clears it and requires a fresh credential on return', async () => {
    const load = vi.fn(async () => credential());
    const controller = new QRCredentialController(load, () => 1000, async () => confirmation());
    controller.activate('house-1');
    await settle();
    controller.tick();
    await settle();
    controller.deactivate();
    expect(controller.getSnapshot().confirmation).toBeNull();
    controller.activate('house-1');
    await settle();
    expect(load).toHaveBeenCalledTimes(2);
  });
});

test('resuming within the expiry grace period can confirm a scan delivered while backgrounded', async () => {
  let now = 1000;
  const status = vi.fn(async () => confirmation());
  const load = vi.fn(async () => credential('house-1', 'signed.payload', now + 60000));
  const controller = new QRCredentialController(load, () => now, status);
  controller.activate('house-1');
  await settle();
  controller.deactivate();
  now = 65000;
  controller.tick();
  expect(status).not.toHaveBeenCalled();
  controller.activate('house-1');
  await settle();
  expect(controller.getSnapshot().confirmation).toEqual(confirmation());
});

test.each([confirmation('other-credential'), confirmation('signed.payload', 'other-house')])(
  'does not accept unrelated confirmations %j', async result => {
    const controller = new QRCredentialController(async () => credential(), () => 1000, async () => result);
    controller.activate('house-1');
    await settle();
    controller.tick();
    await settle();
    expect(controller.getSnapshot().confirmation).toBeNull();
    expect(controller.getSnapshot().credential).not.toBeNull();
  },
);
