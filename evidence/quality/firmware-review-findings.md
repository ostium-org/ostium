# Firmware review findings

The door firmware's pull requests went through an AI-assisted adversarial review before they merged. For each area of the change, reviewer agents looked for defects. Separate verifier agents then tried to reproduce or refute each finding, and only findings that held up were fixed. Each fix to the firmware code came with a test that fails without it, checked by breaking a copy of the code and running the tests (mutation testing). The pipeline fixes in #34 were checked by running the workflow's shell steps against stub and fake GitHub APIs.

| Pull request | What it changed | Findings | Result |
|---|---|---|---|
| [#32](https://github.com/ostium-org/ostium/pull/32), SCRUM-38 | Signed QR credential check on the door | Round 1: 28 reported across 7 areas, 13 confirmed. Round 2: 4 findings whose checks had failed for network reasons in round 1, 3 hardening changes, and 9 more confirmed when the fixes were reviewed. | All confirmed findings fixed. Tests went from 57 to 119. |
| [#34](https://github.com/ostium-org/ostium/pull/34) | Firmware release pipeline and operations guide | 10 confirmed | All fixed, then rechecked against stub and fake GitHub APIs |
| [#33](https://github.com/ostium-org/ostium/pull/33), SCRUM-68 follow-up (draft) | Camera decodes passed to the QR check | 7 reported, 4 confirmed | All fixed. Tests went from 119 to 122. |

## Findings that changed the design

- **One report queue could starve the other** (#32): failing QR report posts could hold back command events indefinitely. `ReportScheduler` now gives command events the next post after a QR post that will be retried.
- **A used code could be granted twice** (#32): after the backend's clock moved backward, the door could forget a used code too early. Used and revoked IDs are now kept for at least 61 seconds on the door's own clock, and until `occurred_at` passes their expiry.
- **Expiry was checked before the signature check** (#32): a code could unlock up to about one signature check after it expired. The door now reads its clock again after the signature check.
- **Small-order public keys were accepted** (#32): a mistaken key could let anyone forge codes. `makeQrKey` now refuses them.
- **Releases could be skipped** (#34): a merge that GitHub made later for the workflow's token started no run on `main`. `auto-merge` now starts that run itself, or leaves the merge to a person.
- **The camera library left the lock pin undriven** (#33, critical): `ESP32QRCodeReader` set GPIO 14, the lock output, to `INPUT_PULLUP` on every board, so unlocks never reached the lock. The block is removed, and the lock output now sets its pin as an output on every write.

The pull request descriptions list every finding and its test.
