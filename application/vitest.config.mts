import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
      // Tests live outside application/, so resolve this native module from
      // the app before replacing it with a mock in demo-mode tests.
      'expo-crypto': fileURLToPath(import.meta.resolve('expo-crypto')),
    },
  },
  test: {
    environment: 'node',
    include: ['../tests/frontend/**/*.test.ts'],
    setupFiles: ['../tests/frontend/setup.ts'],
    coverage: {
      provider: 'v8',
      include: ['src/services/**/*.ts', 'src/utils/**/*.ts'],
      exclude: ['src/services/token-storage*.ts'],
    },
  },
});
