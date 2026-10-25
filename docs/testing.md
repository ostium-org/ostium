# Testing

Test the Edge Function handlers with `deno task --config backend/supabase/deno.json test` from the repository root. Tests are isolated from the shared Supabase project. Test the shared Expo application once, then verify the tenant QR flow on native and web targets.

Prioritize credential expiry, signature validation, unit/device binding, revocations, device authentication, manager authorization, and offline ESP32 behavior. Treat a device configuration update as a separate test case from local QR validation: a valid cached credential must work while the backend is unavailable.

Before a pull request, run the affected component's documented test command and perform a hardware check for relay control changes.

## Frontend unit tests

The frontend suite lives in the repository-root `tests/frontend/` directory. It tests login form validation, Supabase session restoration and refresh, active-profile validation, chunked secure storage, Edge Function requests and error handling, and QR credential parsing. These are service and utility tests; screens, native storage, and browser flows are outside this suite.

Install application dependencies, then run from `application/` using Node.js 22.13 or newer and npm:

```sh
npm ci
npm run test
npm run test:watch
npm run test:coverage
npm run test:typecheck
```

`application/vitest.config.mts` points the runner at the root test directory. The test-specific TypeScript configuration extends the app configuration and resolves `@/` imports to `application/src/`. The setup file mocks the Supabase service with a fixed HTTPS project URL and SDK client before services load, and disables development demo mode. Tests mock `fetch`, block unexpected network calls, use fixed clocks or simulated timer callbacks, and restore shared state between cases. No running backend, emulator, browser, or environment file is needed; after installing dependencies, tests run offline.

Coverage reports cover service and utility modules, excluding platform auth-storage adapters; they do not cover the whole application. There is no required coverage threshold. For runner configuration details, see the [Vitest configuration documentation](https://vitest.dev/config/).

Also run `npx tsc --noEmit` and `npm run lint` for the application. These checks cover the app source separately from the unit suite; resolve any reported source errors before merging.
