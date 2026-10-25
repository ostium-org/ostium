import { describe, expect, test } from 'vitest';

import { createDemoQRCode, remainingQRSeconds } from '@/utils/qr-code';

describe('QR mock payload and countdown', () => {
  test('marks mock data as demo with the requested house and a 60-second lifetime', () => {
    expect(createDemoQRCode('house-2', 'unique-id', 1000)).toEqual({
      type: 'ostium-demo', houseId: 'house-2', id: 'unique-id', issuedAt: 1000, expiresAt: 61000,
    });
  });

  test('uses the response expiry, rounds partial seconds up, and clamps expired values', () => {
    expect(remainingQRSeconds(120000, 0)).toBe(120);
    expect(remainingQRSeconds(60000, 59999)).toBe(1);
    expect(remainingQRSeconds(60000, 60000)).toBe(0);
    expect(remainingQRSeconds(60000, 90000)).toBe(0);
  });
});
