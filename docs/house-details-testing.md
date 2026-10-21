# SCRUM-27: House details validation

## Design
House cards open a details route using the selected house ID.
The list and details screens share a Home navigation stack inside
the signed-in `(app)` group. Property information uses existing mock data.

## Manual browser checks
| Check | Result |
| --- | --- |
| Maple House and Harbour View display their own details | Pass |
| Direct house link loads after refresh while signed in | Pass |
| Unknown house ID displays “House not found” | Pass |
| Signed-out access to house details redirects to login | Pass |
| House navigation works after signing back in | Pass |

## Automated checks
- Application TypeScript check: passed.
- Existing frontend tests: 128 passed, 0 failed.
- Lint for Home routes and native-tabs component: passed.
- Full-project lint: failed with 8 errors and 3 warnings in other files.

## Limitations
- Property data is mocked.
- Native-device behaviour has not been tested.
- Existing theme preference resets after browser refresh.