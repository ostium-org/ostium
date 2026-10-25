# Testing

Test the backend with Python's built-in `unittest` until a larger test suite justifies additional tooling. Test the shared Expo application once, then verify the tenant QR flow on native and web targets.

Prioritize credential expiry, signature validation, unit/device binding, revocations, device authentication, manager authorization, and offline ESP32 behavior. Treat a device configuration update as a separate test case from local QR validation: a valid cached credential must work while the backend is unavailable.

Before a pull request, run the affected component's documented test command and perform a hardware check for relay control changes.

## Frontend unit tests

The frontend suite lives in the repository-root `tests/frontend/` directory. It tests login form validation, saved-session parsing (including expiry boundaries), user parsing, API requests and error handling, and login response validation. These are service and utility tests; screens, native storage, and browser flows are outside this suite.

Install application dependencies, then run from `application/` using Node.js 22.13 or newer and npm:

```sh
npm ci
npm run test
npm run test:watch
npm run test:coverage
npm run test:typecheck
```

`application/vitest.config.mts` points the runner at the root test directory. The test-specific TypeScript configuration extends the app configuration and resolves `@/` imports to `application/src/`. The setup file sets a fixed HTTPS API URL and disables development mock login before services load. Tests mock `fetch`, block unexpected network calls, use fixed clocks or simulated timer callbacks, and restore shared state between cases. No running backend, emulator, browser, or environment file is needed; after installing dependencies, tests run offline.

Coverage reports cover service and utility modules, excluding platform token-storage adapters; they do not cover the whole application. There is no required coverage threshold. For runner configuration details, see the [Vitest configuration documentation](https://vitest.dev/config/).

Also run `npx tsc --noEmit` and `npm run lint` for the application. These checks cover the app source separately from the unit suite; resolve any reported source errors before merging.

## Firmware unit tests

The firmware suite (SCRUM-51) tests the door unit's core logic in `hardware/esp32/lib/ostium_core/`: lock and unlock timing, command checks, event reports, timestamp parsing, poll backoff, and reading response bodies with a size cap and deadlines. Each rule in the [lock command contract](firmware/lock-command-v0.md) has at least one test, and the test names match the contract. The tests live in `hardware/esp32/test/test_core/`.

The suite runs on a laptop in PlatformIO's native environment, so it needs no board, backend, or network after the first run. Fakes stand in for the lock output, clock, relock timer, and network. The native project is `hardware/esp32/native-tests/`, separate from the board's configuration.

To run the suite, install PlatformIO Core 6 (for example, with `brew install platformio`), and then run this from the repository root:

```sh
pio test -d hardware/esp32/native-tests
```

The first run downloads the native platform, ArduinoJson, and Unity. The command exits with a nonzero status when any test fails. The build treats compiler warnings as errors.

The `test_backend_*` tests replay responses recorded from the real FastAPI door routes, in `hardware/esp32/test/test_core/backend_responses.h`. After any change to `backend/app/routers/access.py` or `backend/app/routers/auth.py`, record them again and rerun the suite. To record them, install the backend's requirements (see [backend setup](../backend/readme.md)), and then run this from the repository root:

```sh
python hardware/esp32/tools/capture_backend_responses.py
```

The tool fixes the clock and the command IDs, so the file changes only when the backend's behavior changes. It checks the status of every call, and if a status isn't the one it expects, it stops with an error and leaves the file unchanged.

`test_core` defines its own `main()`, so it builds only for the native environment. The board project, `hardware/esp32/platformio.ini`, sets `test_ignore = test_core`, so `pio test` for the board skips this suite.
