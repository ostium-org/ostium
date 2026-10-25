# SCRUM-27: House details validation

## Design
House cards open a details route using the selected house ID.
The list and details screens share a Home navigation stack inside
the signed-in `(app)` group. Live property information now comes from Supabase
units through `useHouses`; hardcoded sample houses have been removed, including
in demo mode.

Admins and managers can list active units. Tenants need a current active grant;
future, expired, and inactive grants are excluded. Existing Supabase RLS remains
the database authorization boundary. Details and QR selection use the same real
unit IDs as the list, and screen focus/pull-to-refresh reloads data. The QR image
remains a placeholder. Local membership editing is restricted to demo mode.

## Original manual browser checks (before Supabase house loading)
| Check | Result |
| --- | --- |
| Maple House and Harbour View display their own details | Pass |
| Direct house link loads after refresh while signed in | Pass |
| Unknown house ID displays “House not found” | Pass |
| Signed-out access to house details redirects to login | Pass |
| House navigation works after signing back in | Pass |

## Current automated checks
- Frontend suite: 108 passed, including 12 house-loading tests using real SDK
  query construction against a mocked HTTP transport.
- Test TypeScript check and lint for changed house screens/services: passed.
- Expo web export: passed.
- Full-project TypeScript/lint checks still fail in existing unrelated files.

## Device checks after reloading Expo
- Sign in as an admin/manager and confirm active Supabase units are listed.
- Sign in as a tenant and confirm only units with current grants are listed.
- Open a real unit ID, navigate to QR selection, and pull down to refresh Home.
- Disable network access and confirm loading errors offer Retry without showing
  sample houses; restore the network and retry.

## Limitations
- Live membership editing and device telemetry are not connected.
- Native-device behaviour has not been tested.
- Existing theme preference resets after browser refresh.
