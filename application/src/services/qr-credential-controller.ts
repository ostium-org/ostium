import type { QRCredential } from '@/services/qr-credentials';
import { remainingQRSeconds } from '@/utils/qr-code';

type State = {
  active: boolean;
  houseId: string | undefined;
  credential: QRCredential | null;
  seconds: number;
  loading: boolean;
  error: string | null;
};

// Owns async request lifetimes independently of React and native timers.
export class QRCredentialController {
  private state: State = { active: false, houseId: undefined, credential: null, seconds: 0, loading: false, error: null };
  private listeners = new Set<() => void>();
  private active = false;
  private generation = 0;

  constructor(
    private load: (houseId: string) => Promise<QRCredential>,
    private now: () => number = Date.now,
  ) {}

  getSnapshot = () => this.state;
  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  };

  private publish(update: Partial<State>) {
    this.state = { ...this.state, ...update };
    this.listeners.forEach((listener) => listener());
  }

  activate(houseId: string) {
    this.active = true;
    if (houseId !== this.state.houseId) {
      this.generation++;
      this.publish({ active: true, houseId, credential: null, seconds: 0, loading: false, error: null });
    } else {
      const seconds = this.state.credential
        ? remainingQRSeconds(this.state.credential.expiresAt, this.now()) : 0;
      this.publish({ active: true, seconds, credential: seconds ? this.state.credential : null });
    }
    this.tick();
  }

  deactivate() {
    this.active = false;
    this.generation++;
    this.publish({ active: false, loading: false });
  }

  tick = () => {
    if (!this.active || !this.state.houseId) return;
    const remaining = this.state.credential
      ? remainingQRSeconds(this.state.credential.expiresAt, this.now()) : 0;
    if (remaining !== this.state.seconds) this.publish({ seconds: remaining });
    if (!remaining && !this.state.loading && !this.state.error) void this.refresh();
  };

  refresh = async () => {
    const houseId = this.state.houseId;
    if (!this.active || !houseId || this.state.loading) return;
    const generation = ++this.generation;
    this.publish({ credential: null, seconds: 0, loading: true, error: null });
    try {
      const credential = await this.load(houseId);
      if (!this.active || generation !== this.generation) return;
      const now = this.now();
      if (credential.houseId !== houseId || credential.expiresAt <= now) {
        throw new Error('The QR credential has expired or belongs to another house.');
      }
      this.publish({
        credential,
        seconds: remainingQRSeconds(credential.expiresAt, now),
        loading: false,
      });
    } catch (error) {
      if (!this.active || generation !== this.generation) return;
      this.publish({
        credential: null, seconds: 0, loading: false,
        error: error instanceof Error ? error.message : 'Unable to load the QR code.',
      });
    }
  };
}
