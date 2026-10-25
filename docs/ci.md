# Continuous integration (GitHub Actions)

The workflow in `.github/workflows/ci.yml` covers Assignment 1 Part C. A push to a pull request starts the build jobs. If every build passes, the test jobs run. If every test passes in a pull request that targets `main`, the `auto-merge` job merges the pull request.

| Stage | Job | What it runs |
|---|---|---|
| build | `firmware-build` | Compiles the ESP32-S3 firmware in both board builds (`pio run`). |
| build | `backend-build` | Installs the backend's requirements, compiles it, and imports the FastAPI app. |
| build | `frontend-build` | Installs the app's dependencies and type-checks the Expo app (`tsc --noEmit`). |
| test | `firmware-test` | Runs the 54 firmware unit tests on the native platform (`pio test`). |
| test | `backend-test` | Runs the backend's unit tests (`python -m unittest`). |
| test | `frontend-test` | Type-checks and runs the frontend unit tests (`bun test`). |
| merge | `auto-merge` | Runs only in a pull request into `main`. It merges the pull request (`gh pr merge`). |

GitHub Actions has no stages, so each test job lists the three build jobs in `needs`, and `auto-merge` lists the three test jobs. The graph on the run's **Summary** page shows the three stages as three columns.

The workflow runs in these cases:

- Someone opens, reopens, or pushes to a pull request, or marks a draft pull request as ready for review. Every pull request runs the build and test jobs, but only a pull request into `main` runs `auto-merge`. GitHub reads the workflow from the pull request's merge commit, which combines its branch with its base, so the workflow runs only if that commit contains `ci.yml`. A pull request with a merge conflict has no merge commit, so it doesn't run the workflow until you resolve the conflict.
- Someone pushes to `main`. A merge that the `auto-merge` job makes doesn't start a run, because GitHub doesn't start workflows for pushes made with a workflow's own token.
- You start it by hand, after `ci.yml` is on `main`. On the repository's **Actions** tab, click **CI** in the sidebar, click **Run workflow**, choose a branch, and click **Run workflow** again. A manual run builds and tests that branch, but it doesn't run `auto-merge`.

The firmware and frontend test jobs upload their JUnit reports as the `firmware-test-report` and `frontend-test-report` artifacts. You can download them from the run's **Summary** page.

## How auto-merge works

The `auto-merge` job runs `gh pr merge --merge --auto --match-head-commit <sha>` with the workflow's own token:

- If nothing else blocks the pull request, GitHub merges it right away with a merge commit. `--match-head-commit` makes GitHub refuse that merge if someone pushed to the branch after the run started. The push also starts a new run, which cancels the older one.
- If `main` requires more, such as an approving review, GitHub turns on auto-merge for the pull request and merges it when the requirement is met. After auto-merge is on, a push from someone with write access leaves it on, and GitHub merges the new commit once its required checks and approvals pass.
- A draft pull request, or a pull request from a fork, isn't merged.
- The workflow's token can't get the `workflows` permission. GitHub lets it merge a change to a file in `.github/workflows/` only if a file with the same path and the same contents already exists on another branch. The CI pull request meets this rule, because its branch holds the same `ci.yml`. If a later pull request changes `ci.yml` after `main` has changed it too, the job fails with `refusing to allow a GitHub App to create or update workflow`. To fix it, merge `main` into the branch and push, or merge the pull request yourself.

## Set up GitHub and merge the CI pull request

A repository admin does these steps once, in this order, on the `ostium-org/ostium` repository:

1. **Allow auto-merge.** Go to **Settings** > **General** > **Pull Requests**, and select **Allow auto-merge**. Keep **Allow merge commits** selected, because the job merges with a merge commit. If auto-merge isn't allowed, the job fails whenever a pull request has to wait for something, such as a review.
2. **Push the CI branch.** Commit the workflow and this page on `scrum-53-github-actions`, and then push the branch:

   ```sh
   git push -u origin scrum-53-github-actions
   ```

3. **Open the CI pull request.** To show only the CI changes, open the pull request with base `scrum-54-firmware-design`:

   ```sh
   gh pr create --base scrum-54-firmware-design --head scrum-53-github-actions
   ```

   The build and test jobs run, and `auto-merge` is skipped, because the base isn't `main`. The CI branch is stacked on the firmware branches, so this run also builds and tests the code in pull requests #9, #11, #12, and #14.
4. **Merge the firmware pull requests.** Merge pull requests #9, #11, #12, and #14, in that order. After each merge, change the next pull request's base to `main`. These pull requests don't run the workflow, because neither their branches nor `main` contain `ci.yml`.
5. **Merge the CI pull request.** Change the CI pull request's base to `main`. Changing the base doesn't start a run, so click **Close pull request**, and then click **Reopen pull request**. The new run builds, tests, and merges the CI pull request.
6. **Protect `main`.** Go to **Settings**. In the sidebar, under **Code, planning, and automation**, click **Rulesets**, and then click **Rulesets** again. Click **New ruleset** > **New branch ruleset**:
   - Set **Ruleset Name** to `main` and **Enforcement status** to **Active**.
   - Under **Target branches**, click **Add target** > **Include default branch**.
   - Select **Require a pull request before merging**. If you want a teammate's review before each merge, set **Required approvals** to `1`, and auto-merge waits for the approval. Also select **Dismiss stale pull request approvals when new commits are pushed**. Otherwise, an approval of an older commit still counts after a push.
   - Select **Require status checks to pass**. Click **Add checks**, and add `firmware-build`, `backend-build`, `frontend-build`, `firmware-test`, `backend-test`, and `frontend-test`. If GitHub doesn't suggest a check, type its exact name, and click **+**.
   - Click **Create**.

   With this ruleset, nobody can merge a pull request into `main` until all six checks pass, even by hand. Don't create it before step 5. Pull requests #9, #11, #12, and #14 never report these checks, so an active ruleset stops them from merging, and a ruleset has no bypass unless you add one.
7. **Fill the caches on `main`.** On the **Actions** tab, click **CI** > **Run workflow**, keep `main` as the branch, and click **Run workflow**. Every pull request can restore the caches that this run saves. For details, see [Notes](#notes).

If your team can't review the firmware pull requests before the deadline, open the CI pull request with base `main` in step 3 instead, and skip steps 4 and 5. That pull request carries every firmware commit, so its run builds, tests, and merges all of them into `main` at once. Afterward, close any of pull requests #9, #11, #12, and #14 that GitHub doesn't mark as merged.

You don't need to change **Settings** > **Actions** > **General** > **Workflow permissions**. The `auto-merge` job asks for the write permissions it needs in the workflow file.

For the Assignment 1 report, capture the run's graph with all three stages, the `auto-merge` job's log, and the merged pull request.

## Run the jobs on your computer

Each job's commands also run in public Docker images, so you can check a change before you push it. Run each command from the repository root. The named volumes keep the Linux build output and packages out of your working copy, and they keep the downloads for the next run.

To run the firmware build and tests:

```sh
docker run --rm -v "$PWD":/repo -v ostium-platformio:/root/.platformio -v ostium-pio-board:/repo/hardware/esp32/.pio -v ostium-pio-native:/repo/hardware/esp32/native-tests/.pio -w /repo python:3.12.14-bookworm sh -c 'pip install -q platformio==6.2.0 && pio run -d hardware/esp32 -e esp32s3 && pio run -d hardware/esp32 -e esp32s3-bench && pio test -d hardware/esp32/native-tests'
```

To run the backend build and tests:

```sh
docker run --rm -v "$PWD":/repo -w /repo/backend python:3.12.14-bookworm sh -c 'pip install -q -r requirements.txt && python -c "import app.main" && python -m unittest discover -s tests'
```

To run the frontend build and tests:

```sh
docker run --rm -v "$PWD":/repo -v ostium-node-modules:/repo/application/node_modules -w /repo/application oven/bun:1.3.14 sh -c 'bun install --frozen-lockfile && printf "/// <reference types=\"expo/types\" />\n" > expo-env.d.ts && bunx tsc --noEmit && bun run test:typecheck && bun test'
```

## Notes

- **The frontend build is a type check.** The Expo app has no separate compile step in CI. Device builds through EAS are outside this workflow.
- **The `frontend-build` job writes `expo-env.d.ts`.** `expo start` normally writes this file, and `.gitignore` leaves it out. It loads Expo's types, which declare the app's CSS imports, so `tsc` fails on a clean checkout without it. The test type check uses `tests/frontend/tsconfig.json`, which leaves this file out, so `frontend-test` doesn't need it.
- **The workflow caches downloads.** The firmware jobs cache PlatformIO's toolchains, the backend jobs cache pip downloads, and the frontend jobs cache Bun's package downloads. The first firmware build downloads the ESP32-S3 toolchain, about 1 GB. GitHub keeps a cache that a pull request saves for that pull request only. Later runs of the same pull request restore it, but a new pull request can restore only the caches saved on `main` or on its base branch. A merge by `auto-merge` doesn't start a run on `main`, so run the workflow on `main` by hand, as in step 7. Do it again after `hardware/esp32/platformio.ini` or `application/bun.lock` changes, or after the caches go 7 days without use.
- **The repository is public,** so GitHub Actions minutes are free.
