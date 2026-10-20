export const QR_LIFETIME_MS = 60_000;

// Demo data only. Real door credentials must be issued and signed by the backend.
export type DemoQRCode = {
  type: 'ostium-demo';
  houseId: string;
  id: string;
  issuedAt: number;
  expiresAt: number;
};

export function createDemoQRCode(houseId: string, id: string, now: number): DemoQRCode {
  return { type: 'ostium-demo', houseId, id, issuedAt: now, expiresAt: now + QR_LIFETIME_MS };
}

export function remainingQRSeconds(expiresAt: number, now: number): number {
  return Math.max(0, Math.ceil((expiresAt - now) / 1000));
}
