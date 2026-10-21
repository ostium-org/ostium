import { ApiError, isMockApiEnabled, request } from '@/services/api';
import { createDemoQRCode } from '@/utils/qr-code';

export type QRCredential = {
  houseId: string;
  payload: string;
  expiresAt: number;
  isDemo: boolean;
};

export function parseQRCredential(data: unknown, houseId: string, now = Date.now()): QRCredential {
  const bad = () => new ApiError('bad-response', 'The server returned an invalid QR credential.');
  if (!data || typeof data !== 'object' || Array.isArray(data)) throw bad();
  const value = data as Record<string, unknown>;
  if (value.house_id !== houseId || typeof value.payload !== 'string' || !value.payload.trim()) throw bad();
  // Require a timestamp with a timezone so expiry does not depend on the phone's timezone.
  if (typeof value.expires_at !== 'string' ||
      !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(value.expires_at)) throw bad();
  const expiresAt = Date.parse(value.expires_at);
  if (!Number.isFinite(expiresAt) || expiresAt <= now) throw bad();
  return { houseId, payload: value.payload, expiresAt, isDemo: false };
}

export async function getQRCredential(houseId: string): Promise<QRCredential> {
  if (isMockApiEnabled()) {
    // Keep native random generation out of the API path and the screen.
    const { randomUUID } = await import('expo-crypto');
    const demo = createDemoQRCode(houseId, randomUUID(), Date.now());
    return { houseId, payload: JSON.stringify(demo), expiresAt: demo.expiresAt, isDemo: true };
  }
  const response = await request(`/api/v1/houses/${encodeURIComponent(houseId)}/qr-credentials`, {
    method: 'POST',
  });
  return parseQRCredential(response, houseId);
}
