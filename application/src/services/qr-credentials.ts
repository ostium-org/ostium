import { ApiError, isMockApiEnabled, request } from '@/services/api';
import { createDemoQRCode } from '@/utils/qr-code';

export type QRCredential = {
  id: string;
  houseId: string;
  payload: string;
  expiresAt: number;
  isDemo: boolean;
};

export function parseQRCredential(data: unknown, houseId: string, now = Date.now()): QRCredential {
  const bad = () => new ApiError('bad-response', 'The server returned an invalid QR credential.');
  if (!data || typeof data !== 'object' || Array.isArray(data)) throw bad();
  const value = data as Record<string, unknown>;
  if (typeof value.credential_id !== 'string' || !/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(value.credential_id)) throw bad();
  if (value.house_id !== houseId || typeof value.payload !== 'string' || !value.payload.trim()) throw bad();
  // Require a timestamp with a timezone so expiry does not depend on the phone's timezone.
  if (typeof value.expires_at !== 'string' ||
      !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(value.expires_at)) throw bad();
  const expiresAt = Date.parse(value.expires_at);
  if (!Number.isFinite(expiresAt) || expiresAt <= now) throw bad();
  return { id: value.credential_id, houseId, payload: value.payload, expiresAt, isDemo: false };
}

export async function getQRCredential(houseId: string): Promise<QRCredential> {
  if (isMockApiEnabled()) {
    // Keep native random generation out of the API path and the screen.
    const { randomUUID } = await import('expo-crypto');
    const demo = createDemoQRCode(houseId, randomUUID(), Date.now());
    return { id: demo.id, houseId, payload: JSON.stringify(demo), expiresAt: demo.expiresAt, isDemo: true };
  }
  const response = await request(`/api/v1/houses/${encodeURIComponent(houseId)}/qr-credentials`, {
    method: 'POST',
  });
  return parseQRCredential(response, houseId);
}

export type QRAccessConfirmation = {
  id: string;
  credentialId: string;
  houseId: string;
  occurredAt: number;
};

export async function getQRAccessStatus(houseId: string, credentialId: string): Promise<QRAccessConfirmation | null> {
  const data = await request<Record<string, unknown>>(
    `/api/v1/houses/${encodeURIComponent(houseId)}/qr-credentials/${encodeURIComponent(credentialId)}/status`,
  );
  //added this so malformed or unrelated status responses can never turn into success.
  const bad = () => new ApiError('bad-response', 'The server returned an invalid access confirmation.');
  if (!data || data.house_id !== houseId || data.credential_id !== credentialId) throw bad();
  if (data.status === 'pending' && data.event === null) return null;
  const event = data.event as Record<string, unknown> | null;
  if (data.status !== 'granted' || !event || typeof event.id !== 'string' || !event.id.trim() ||
    typeof event.occurred_at !== 'string' ||
    !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(event.occurred_at)) throw bad();
  const occurredAt = Date.parse(event.occurred_at);
  if (!Number.isFinite(occurredAt)) throw bad();
  return { id: event.id, credentialId, houseId, occurredAt };
}
