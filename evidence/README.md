# Evidence

Machine-made results for Assignment 2, grouped by part. The report's evidence index points here. Each module adds its own files under the same folders.

- `tests/`: test reports and test run logs (Part B)
- `quality/`: static analysis and review results (Part B)
- `deployment/`: pipeline logs and release records (Part C)

## Door unit firmware

All firmware files come from commit [`a798359`](https://github.com/ostium-org/ostium/commit/a7983599a29cbe73a31eef298e8252b985db87e1) on `main`, and its CI run, [run #21](https://github.com/ostium-org/ostium/actions/runs/45537793630), which published release [`firmware-r21`](https://github.com/ostium-org/ostium/releases/tag/firmware-r21) on November 25, 2026.

| Claim | File |
|---|---|
| All 119 firmware unit tests pass in CI, with no errors or failures. | [`tests/firmware-tests-r21.xml`](tests/firmware-tests-r21.xml), the JUnit report attached to the release |
| CI checks the QR fixtures against the backend, then runs the tests. | [`tests/firmware-test-run21.log`](tests/firmware-test-run21.log), the `firmware-test` job log |
| Both board builds compile and are packaged on every run. | [`deployment/firmware-build-run21.log`](deployment/firmware-build-run21.log), the `firmware-build` job log |
| A green run on `main` publishes the firmware as a release. | [`deployment/firmware-release-run21.log`](deployment/firmware-release-run21.log), the `firmware-release` job log |
| The release files match their published checksums. | [`deployment/firmware-r21-SHA256SUMS`](deployment/firmware-r21-SHA256SUMS) |
| Every job in run #21 passed, and the run published the release. | [`deployment/firmware-ci-run21.png`](deployment/firmware-ci-run21.png), a screenshot of the run page |
| The release holds both board images, their checksums, and the test report. | [`deployment/firmware-r21-release.png`](deployment/firmware-r21-release.png), a screenshot of the release page |
| The code builds with warnings treated as errors, on Clang and GCC. | [`quality/firmware-static-checks.txt`](quality/firmware-static-checks.txt) |
| Adversarial review findings were confirmed and fixed. | [`quality/firmware-review-findings.md`](quality/firmware-review-findings.md) |

The logs come from GitHub Actions. GitHub masks the workflow token in them as `***`, and the firmware images hold only placeholder settings, so no secret appears here. The screenshots were taken signed out on November 25, 2026, so they show only what any visitor sees.
