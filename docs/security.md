# Security

QR credentials must be short-lived, signed, bound to the intended unit or device, and validated locally by the ESP32-CAM. Active managers/admins may also issue authenticated ten-second remote lock/unlock commands, which devices poll and acknowledge. Remote commands supplement the offline QR flow.

Store the QR signing key and each device credential outside the repository. Use HTTPS for all user and device traffic, Supabase Auth for password handling, server-side QR signing and device verification, short-lived user tokens, and role checks on every manager action.

The ESP32 stores only the configuration and revocation data required for offline validation. Treat firmware updates, relay control, device registration, and snapshot access as security-sensitive changes. A revoked credential takes effect on a device after its next successful configuration poll.
