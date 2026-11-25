# Continuous integration (GitHub Actions)

The workflow in `.github/workflows/ci.yml` covers Assignment 1 Part C, and delivery for Assignment 2 Part C. A push to a pull request starts the build jobs. If every build passes, the test jobs run. If every test passes in a pull request that targets `main`, the `auto-merge` job merges the pull request and starts a run on `main`. If the pull request has to wait for something, such as a review, the job leaves the merge to you. When a run on `main` passes, the `firmware-release` job publishes the door firmware as a GitHub release.

| Stage | Job | What it runs |
|---|---|---|
| build | `firmware-build` | Compiles the ESP32-S3 firmware in both board builds (`pio run`), then packages both with `hardware/esp32/tools/package_firmware.sh` and uploads the `firmware-release` artifact. |
| build | `backend-build` | Type-checks and lints the Supabase Edge Functions with Deno 2. |
| build | `frontend-build` | Installs the app's dependencies and type-checks the Expo app (`tsc --noEmit`). |
| test | `firmware-test` | Checks that the generated QR fixture headers match the Deno backend (`capture_qr_backend.ts --check`), then runs the 119 firmware unit tests on the native platform (`pio test`). |
| test | `backend-test` | Runs the Edge Function handler tests (`deno test`). |
| test | `frontend-test` | Type-checks and runs the frontend unit tests (`vitest run`). |
| merge | `auto-merge` | Runs only in a pull request into `main`. It merges the pull request (`gh pr merge`), then starts a run on `main` (`gh workflow run`). If the pull request has to wait, it leaves the merge to you. |
| deliver | `firmware-release` | Runs only on `main`, after every test passes. It publishes the `firmware-release` artifact as a GitHub release, `firmware-r<run number>`, tagged at the tested commit. |

GitHub Actions has no stages, so each test job lists the three build jobs in `needs`, and `auto-merge` and `firmware-release` both list the three test jobs. The graph on the run's **Summary** page shows three columns: the build jobs, the test jobs, and then `auto-merge` and `firmware-release` together. A pull request run skips `firmware-release`, and a run on `main` skips `auto-merge`, so the graph shows the skipped job grayed out.

The workflow runs in these cases:

- Someone opens, reopens, or pushes to a pull request, or marks a draft pull request as ready for review. Every pull request runs the build and test jobs, but only a pull request into `main` runs `auto-merge`. GitHub reads the workflow from the pull request's merge commit, which combines its branch with its base, so the workflow runs only if that commit contains `ci.yml`. A pull request with a merge conflict has no merge commit, so it doesn't run the workflow until you resolve the conflict.
- Someone pushes to `main`, including when you merge a pull request yourself. A merge that the `auto-merge` job makes doesn't start a push run, because GitHub doesn't start workflows for pushes made with a workflow's own token. GitHub does start a run for `workflow_dispatch`, so `auto-merge` starts one on `main` after it merges. That run builds and tests the merged code, and `firmware-release` publishes it.
- You start it by hand, after `ci.yml` is on `main`. On the repository's **Actions** tab, click **CI** in the sidebar, click **Run workflow**, choose a branch, and click **Run workflow** again. A manual run builds and tests that branch, but it doesn't run `auto-merge`. A manual run on `main` also publishes a firmware release, even for a commit that already has one.

The firmware and frontend test jobs upload their JUnit reports as the `firmware-test-report` and `frontend-test-report` artifacts. You can download them from the run's **Summary** page.

## Firmware releases

Each release on the repository's **Releases** page holds:

- `ostium-door-esp32s3-r<N>.zip` and `ostium-door-esp32s3-bench-r<N>.zip`: the bootloader, partition table, `boot_app0.bin`, and app image for a full flash, the matching `firmware.elf` for decoding a crash backtrace, and `FLASHING.txt` with the `esptool.py` command.
- `SHA256SUMS`: checksums of both zips.
- `firmware-tests.xml`: the native test report for that commit.

CI builds without `include/secrets.h`, so the images hold the placeholder settings from `secrets.example.h`: no Wi-Fi, no device token, and QR access off. The repository is public, so a release must never hold real secrets. The packaging script stops if `secrets.h` exists, or if an image doesn't hold the placeholder Wi-Fi password and device token. Use a release image only to check that a board boots and passes the QR self-test. To deploy a door, build the release's commit with your own `secrets.h`. To roll back, build the previous release's commit the same way. The [operations guide](operations-guide.md#door-unit-firmware) gives the steps and the health check.

Each run on `main` publishes its own release. If you re-run a run on `main`, its release keeps its tag and gets the new files. If you re-run an older run after a newer release exists, its release isn't marked **Latest**, so **Latest** stays on the newest firmware. A re-run on `main` also cancels any newer run on `main` that's still in progress, because they share a concurrency group.

To package locally, build both board builds with PlatformIO on your computer, with `include/secrets.h` moved out of the repository, and then run this from the repository root:

```sh
hardware/esp32/tools/package_firmware.sh dist r0 "$(git rev-parse HEAD)"
```

The script packages the images in `hardware/esp32/.pio/build`, so run it where you built. After a build with `secrets.h`, moving the file isn't enough: the script stops until you run both `pio run` builds again. The Docker command in [Run the jobs on your computer](#run-the-jobs-on-your-computer) builds into a Docker volume that the script can't see from your computer, so that command packages inside the container.

## How auto-merge works

The `auto-merge` job runs `gh pr merge --merge --auto --match-head-commit <sha>` with the workflow's own token:

- If nothing else blocks the pull request, GitHub merges it right away with a merge commit. `--match-head-commit` makes GitHub refuse that merge if someone pushed to the branch after the run started. The push also starts a new run, which cancels the older one.
- If `main` requires more, such as an approving review, gh turns on GitHub's auto-merge, and the job's next step turns it back off. GitHub's later merge would count as a push with the workflow's token, so it would start no run on `main` and publish no release. The job ends with a warning on the run's **Summary** page. Once the requirement is met, merge the pull request yourself. Your merge is a push to `main`, so it starts a run there, and `firmware-release` publishes it. If the approval comes before the tests finish, the job merges right away instead.
- After the merge, the job starts a run on `main` with `gh workflow run`. If the job can't read the pull request's state, turn auto-merge off, or start that run, it fails, so you see any merge that won't get a release.
- A draft pull request, or a pull request from a fork, isn't merged.
- The workflow's token can't get the `workflows` permission. GitHub lets it merge a change to a file in `.github/workflows/` only if a file with the same path and the same contents already exists on another branch. The CI pull request meets this rule, because its branch holds the same `ci.yml`. If a later pull request changes `ci.yml` after `main` has changed it too, the job fails with `refusing to allow a GitHub App to create or update workflow`. To fix it, merge `main` into the branch and push, or merge the pull request yourself.

## Set up GitHub and get the workflow onto `main`

A repository admin does these steps once, in this order, on the `ostium-org/ostium` repository:

1. **Allow auto-merge.** Go to **Settings** > **General** > **Pull Requests**, and select **Allow auto-merge**. Keep **Allow merge commits** selected, because the job merges with a merge commit. If auto-merge isn't allowed, the job fails whenever a pull request has to wait for something, such as a review.
2. **Open the firmware pull request into `main`.** On October 25, pull request #9 merged into `main`, but pull requests #11, #12, #14, and #15 merged into each other's branches, so the core library, the board firmware, the firmware docs, and this workflow never reached `main`. The `scrum-58-firmware-fixes` branch holds all of that work, merged with `main`. Push it, and open a pull request into `main`:

   ```sh
   git push -u origin scrum-58-firmware-fixes
   ```

   ```sh
   gh pr create --base main --head scrum-58-firmware-fixes
   ```

   Its run builds and tests all three modules, and then `auto-merge` merges it, because it targets `main`. The workflow's token may add `ci.yml` to `main`, because the same file is already on the pull request's branch.
3. **Protect `main`.** After the pull request from step 2 merges, go to **Settings**. In the sidebar, under **Code, planning, and automation**, click **Rulesets**, and then click **Rulesets** again. Click **New ruleset** > **New branch ruleset**:
   - Set **Ruleset Name** to `main` and **Enforcement status** to **Active**.
   - Under **Target branches**, click **Add target** > **Include default branch**.
   - Select **Require a pull request before merging**. If you want a teammate's review before each merge, set **Required approvals** to `1`. The `auto-merge` job then can't merge a pull request that's waiting for the approval, so the reviewer merges it after approving, and that merge starts the run on `main` that publishes the release. Also select **Dismiss stale pull request approvals when new commits are pushed**. Otherwise, an approval of an older commit still counts after a push.
   - Select **Require status checks to pass**. Click **Add checks**, and add `firmware-build`, `backend-build`, `frontend-build`, `firmware-test`, `backend-test`, and `frontend-test`. If GitHub doesn't suggest a check, type its exact name, and click **+**.
   - Click **Create**.

   With this ruleset, nobody can merge a pull request into `main` until all six checks pass, even by hand. Don't create it before step 2's pull request merges. A pull request whose branch and base both lack `ci.yml` never reports these checks, so the ruleset would stop it from merging, and a ruleset has no bypass unless you add one.
4. **Fill the caches on `main`.** On the **Actions** tab, click **CI** > **Run workflow**, keep `main` as the branch, and click **Run workflow**. Every pull request can restore the caches that this run saves. Like any run on `main`, it also publishes a firmware release. For details, see [Notes](#notes).

You don't need to change **Settings** > **Actions** > **General** > **Workflow permissions**. The `auto-merge` job asks for the write permissions it needs in the workflow file.

For the Assignment 1 report, capture the run's graph with all three stages, the `auto-merge` job's log, and the merged pull request. For the Assignment 2 report, capture a run on `main`: its third column shows `firmware-release` passed and `auto-merge` skipped. Also capture the release it published on the **Releases** page.

## Run the jobs on your computer

The build and test jobs' commands also run in public Docker images, so you can check a change before you push it. Run each command from the repository root. The named volumes keep the Linux build output and packages out of your working copy, and they keep the downloads for the next run.

To run the firmware build, packaging, and tests, first move `hardware/esp32/include/secrets.h` out of the repository if you have one, because the container sees your working copy and the packaging step stops if the file exists. Then run:

```sh
docker run --rm -e COMMIT="$(git rev-parse HEAD)" -v "$PWD":/repo -v ostium-platformio:/root/.platformio -v ostium-pio-board:/repo/hardware/esp32/.pio -v ostium-pio-native:/repo/hardware/esp32/native-tests/.pio -w /repo python:3.12.14-bookworm sh -c 'apt-get update -qq && apt-get install -y -qq zip >/dev/null && pip install -q platformio==6.2.0 && pio run -d hardware/esp32 -e esp32s3 && pio run -d hardware/esp32 -e esp32s3-bench && hardware/esp32/tools/package_firmware.sh dist r0 "$COMMIT" && pio test -d hardware/esp32/native-tests'
```

The packages land in `dist/` in your working copy, which Git ignores.

The `firmware-test` job also runs the QR fixture check, which needs Deno. To run it:

```sh
docker run --rm -v "$PWD":/repo -w /repo denoland/deno:2 deno run --check --allow-read hardware/esp32/tools/capture_qr_backend.ts --check
```

To run the backend build and tests:

```sh
docker run --rm -v "$PWD":/repo -w /repo denoland/deno:2 sh -c 'deno task --config backend/supabase/deno.json check && deno task --config backend/supabase/deno.json lint && deno task --config backend/supabase/deno.json test'
```

To run the frontend build and tests:

```sh
docker run --rm -v "$PWD":/repo -v ostium-node-modules:/repo/application/node_modules -w /repo/application node:22-bookworm sh -c 'npm ci && printf "/// <reference types=\"expo/types\" />\n" > expo-env.d.ts && npx tsc --noEmit && npm run test:typecheck && npm run test'
```

## Notes

- **The frontend build is a type check.** The Expo app has no separate compile step in CI. Device builds through EAS are outside this workflow.
- **The `frontend-build` job writes `expo-env.d.ts`.** `expo start` normally writes this file, and `.gitignore` leaves it out. It loads Expo's types, which declare the app's CSS imports, so `tsc` fails on a clean checkout without it. The test type check uses `tests/frontend/tsconfig.json`, which leaves this file out, so `frontend-test` doesn't need it.
- **The workflow caches downloads.** The firmware jobs cache PlatformIO's toolchains, the frontend jobs cache npm's package downloads. The first firmware build downloads the ESP32-S3 toolchain, about 1 GB. GitHub keeps a cache that a pull request saves for that pull request only. Later runs of the same pull request restore it, but a new pull request can restore only the caches saved on `main` or on its base branch. Every merge into `main` starts a run there: the run that `auto-merge` starts, or the push run from a merge you make yourself. That run saves the caches on `main`, including new ones after `hardware/esp32/platformio.ini`, `hardware/esp32/native-tests/platformio.ini`, or `application/package-lock.json` changes. Run the workflow on `main` by hand, as in step 4 of [Set up GitHub and get the workflow onto `main`](#set-up-github-and-get-the-workflow-onto-main), only after the caches go 7 days without use. That run also publishes another firmware release of the same commit.
- **The repository is public,** so GitHub Actions minutes are free.
