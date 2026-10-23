# Testing

Test the Edge Function handlers with `deno task --config backend/supabase/deno.json test` from the repository root. Tests are isolated from the shared Supabase project. Test the shared Expo application once, then verify the tenant QR flow on native and web targets.

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
