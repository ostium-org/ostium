# Security

QR credentials must be short-lived, signed, bound to the intended unit or device, and validated locally by the ESP32-S3. Active managers/admins may also issue authenticated ten-second remote lock/unlock commands, which devices poll and acknowledge. Remote commands supplement the offline QR flow.

Store the QR signing key and each device credential outside the repository. Use HTTPS for all user and device traffic, Supabase Auth for password handling, server-side QR signing and device verification, short-lived user tokens, and role checks on every manager action.

The ESP32 stores only the configuration required for offline validation: its unit ID and the QR signing public key, never a private key. Treat firmware updates, relay control, device registration, and snapshot access as security-sensitive changes. No revocation or configuration feed reaches the door yet, so a revoked credential still opens the door until it expires, within 60 seconds. A device whose token or status the backend refuses stops accepting QR codes at its next poll. The door enforces single use, and refuses the test fixture's key outside the bench build. See the [QR access contract](firmware/qr-access-v0.md).
