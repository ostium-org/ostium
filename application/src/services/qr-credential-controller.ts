import type { QRAccessConfirmation, QRCredential } from '@/services/qr-credentials';
import { remainingQRSeconds } from '@/utils/qr-code';

type State = {
  active: boolean;
  houseId: string | undefined;
  credential: QRCredential | null;
  seconds: number;
  loading: boolean;
  error: string | null;
  confirmation: QRAccessConfirmation | null;
  confirmationError: string | null;
};

// Owns async request lifetimes independently of React and native timers.
export class QRCredentialController {
  private state: State = { active: false, houseId: undefined, credential: null, seconds: 0, loading: false, error: null,
    confirmation: null, confirmationError: null };
  private listeners = new Set<() => void>();
  private active = false;
  private generation = 0;
  private scope = 0;
  private tracked = new Map<string, number>();
  private polling = false;
  private nextPoll = 0;

  constructor(
    private load: (houseId: string) => Promise<QRCredential>,
    private now: () => number = Date.now,
    private loadStatus?: (houseId: string, credentialId: string) => Promise<QRAccessConfirmation | null>,
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
      this.scope++;
      this.tracked.clear();
      this.nextPoll = 0;
      this.publish({ active: true, houseId, credential: null, seconds: 0, loading: false, error: null,
        confirmation: null, confirmationError: null });
    } else {
      const seconds = this.state.credential
        ? remainingQRSeconds(this.state.credential.expiresAt, this.now()) : 0;
      this.publish({ active: true, seconds, credential: seconds ? this.state.credential : null });
      if (seconds && this.state.credential && !this.state.credential.isDemo) {
        this.tracked.set(this.state.credential.id, this.state.credential.expiresAt + 15000);
      }
    }
    this.tick();
  }

  deactivate() {
    this.active = false;
    this.generation++;
    this.scope++;
    //added this so brief backgrounding does not lose a scan still inside its grace period.
    this.nextPoll = 0;
    this.publish({ active: false, loading: false, confirmation: null, confirmationError: null });
  }

  tick = () => {
    if (!this.active || !this.state.houseId || this.state.confirmation) return;
    const remaining = this.state.credential
      ? remainingQRSeconds(this.state.credential.expiresAt, this.now()) : 0;
    if (remaining !== this.state.seconds) this.publish({ seconds: remaining });
    if (!remaining && !this.state.loading && !this.state.error) void this.refresh();
    void this.pollStatus();
  };

  refresh = async () => {
    const houseId = this.state.houseId;
    if (!this.active || !houseId || this.state.loading || this.state.confirmation) return;
    const generation = ++this.generation;
    //added this so a scan reported just after rotation can still confirm the old code.
    const previous = this.state.credential;
    if (previous && !previous.isDemo) {
      this.tracked.set(previous.id, Math.min(previous.expiresAt, this.now()) + 15000);
    }
    this.publish({ credential: null, seconds: 0, loading: true, error: null });
    try {
      const credential = await this.load(houseId);
      if (!this.active || generation !== this.generation) return;
      const now = this.now();
      if (credential.houseId !== houseId || credential.expiresAt <= now) {
        throw new Error('The QR credential has expired or belongs to another house.');
      }
      if (!credential.isDemo) this.tracked.set(credential.id, credential.expiresAt + 15000);
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

  retryConfirmation = () => {
    this.nextPoll = 0;
    void this.pollStatus();
  };

  returnToCode = () => {
    if (!this.active || !this.state.confirmation) return;
    this.scope++;
    this.tracked.clear();
    this.nextPoll = 0;
    this.publish({ confirmation: null, confirmationError: null });
    void this.refresh();
  };

  private async pollStatus() {
    const houseId = this.state.houseId;
    if (!this.active || !houseId || !this.loadStatus || this.polling || this.state.confirmation || this.now() < this.nextPoll) return;
    for (const [id, deadline] of this.tracked) {
      if (deadline <= this.now()) this.tracked.delete(id);
    }
    if (!this.tracked.size) return;
    this.polling = true;
    const scope = this.scope;
    this.nextPoll = this.now() + 2000;
    let failed = false;
    try {
      for (const [id, deadline] of this.tracked) {
        if (deadline <= this.now()) continue;
        try {
          const confirmation = await this.loadStatus(houseId, id);
          if (!this.active || scope !== this.scope) return;
          //added this so late responses cannot confirm a different house or a forgotten code.
          if (confirmation && confirmation.houseId === houseId && confirmation.credentialId === id &&
            (this.tracked.get(id) ?? 0) > this.now()) {
            this.generation++;
            this.tracked.clear();
            this.publish({ confirmation, confirmationError: null, credential: null, seconds: 0, loading: false });
            return;
          }
        } catch {
          if (!this.active || scope !== this.scope) return;
          failed = true;
        }
      }
      this.publish({ confirmationError: failed ? 'Access confirmation is unavailable. You can still scan a valid QR code.' : null });
    } finally {
      this.polling = false;
    }
  }
}
