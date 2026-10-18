import { describe, expect, test } from 'bun:test';

import { createDemoQRCode, getCurrentDemoQRCode, remainingQRSeconds } from '@/utils/qr-code';

describe('demo QR codes', () => {
  test('binds demo data to the selected house with a 60-second lifetime', () => {
    expect(createDemoQRCode('house-2', 'unique-id', 1000)).toEqual({
      type: 'ostium-demo', houseId: 'house-2', id: 'unique-id', issuedAt: 1000, expiresAt: 61000,
    });
  });

  test('keeps an unexpired code for the same house without generating another ID', () => {
    const current = createDemoQRCode('house-1', 'first', 1000);
    const next = getCurrentDemoQRCode(current, 'house-1', 60999, () => {
      throw new Error('Should not generate an ID before expiry');
    });
    expect(next).toBe(current);
  });

  test('changes the code and resets its lifetime immediately when the house changes', () => {
    const current = createDemoQRCode('house-1', 'first', 1000);
    const next = getCurrentDemoQRCode(current, 'house-2', 2000, () => 'second');
    expect(next.houseId).toBe('house-2');
    expect(next.id).toBe('second');
    expect(next.expiresAt).toBe(62000);
  });

  test.each([61000, 300000])('rotates at expiry or after returning later at %i', (now) => {
    const current = createDemoQRCode('house-1', 'first', 1000);
    const next = getCurrentDemoQRCode(current, 'house-1', now, () => 'second');
    expect(next.id).toBe('second');
    expect(next.issuedAt).toBe(now);
    expect(next.expiresAt - now).toBe(60000);
  });

  test('generates the initial code when there is no current code', () => {
    expect(getCurrentDemoQRCode(null, 'house-1', 1000, () => 'first'))
      .toEqual(createDemoQRCode('house-1', 'first', 1000));
  });

  test('rounds up partial seconds and clamps expired countdowns to zero', () => {
    expect(remainingQRSeconds(60000, 0)).toBe(60);
    expect(remainingQRSeconds(60000, 59999)).toBe(1);
    expect(remainingQRSeconds(60000, 60000)).toBe(0);
    expect(remainingQRSeconds(60000, 90000)).toBe(0);
  });
});
