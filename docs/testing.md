# Testing

Test the backend with Python's built-in `unittest` until a larger test suite justifies additional tooling. Test the shared Expo application once, then verify the tenant QR flow on native and web targets.

Prioritize credential expiry, signature validation, unit/device binding, revocations, device authentication, manager authorization, and offline ESP32 behavior. Treat a device configuration update as a separate test case from local QR validation: a valid cached credential must work while the backend is unavailable.

Before a pull request, run the affected component's documented test command and perform a hardware check for relay control changes.

## Frontend unit tests

The frontend suite lives in the repository-root `tests/frontend/` directory. It tests login form validation, saved-session parsing (including expiry boundaries), user parsing, API requests and error handling, and login response validation. These are service and utility tests; screens, native storage, and browser flows are outside this suite.

Install application dependencies, then run from `application/` using Bun 1.3.14 or newer:

```sh
bun install --frozen-lockfile
bun run test
bun run test:watch
bun run test:coverage
bun run test:typecheck
```

`application/bunfig.toml` points the runner at the root test directory. The test-specific TypeScript configuration extends the app configuration and resolves `@/` imports to `application/src/`. The preload sets a fixed HTTPS API URL and disables development mock login before services load. Tests mock `fetch`, block unexpected network calls, use fixed clocks or simulated timer callbacks, and restore shared state between cases. No running backend, emulator, browser, or environment file is needed; after installing dependencies, tests run offline.

Coverage reports describe the imported utility and service modules, not the whole application. There is no required coverage threshold. For Bun configuration details, see the [official test configuration documentation](https://bun.com/docs/test/configuration).

Also run `bunx tsc --noEmit` and `bun run lint` for the application. ESLint dependencies/configuration are not set up; that baseline issue is separate from SCRUM-25 and is not repaired by the unit suite.

## Firmware unit tests

The firmware suite (SCRUM-51) tests the door unit's core logic in `hardware/esp32/lib/ostium_core/`: lock and unlock timing, command checks, event reports, timestamp parsing, and poll backoff. Each rule in the [lock command contract](firmware/lock-command-v0.md) has at least one test, and the test names match the contract. The tests live in `hardware/esp32/test/test_core/`.

The suite runs on a laptop in PlatformIO's native environment, so it needs no board, backend, or network after the first run. Fakes stand in for the lock output, clock, relock timer, and network. The native project is `hardware/esp32/native-tests/`, separate from the board's configuration.

To run the suite, install PlatformIO Core 6 (for example, with `brew install platformio`), and then run this from the repository root:

```sh
pio test -d hardware/esp32/native-tests
```

The first run downloads the native platform, ArduinoJson, and Unity. The command exits with a nonzero status when any test fails. The build treats compiler warnings as errors.

`test_core` defines its own `main()`, so it builds only for the native environment. When the board project is added at `hardware/esp32/platformio.ini`, add `test_ignore = test_core` to its board environment, so that `pio test` for the board skips this suite.
