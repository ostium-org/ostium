import { describe, expect, test } from 'vitest';

import { validateLoginForm } from '@/utils/validation';

describe('validateLoginForm', () => {
  test('accepts a valid email and password', () => {
    expect(validateLoginForm('tenant@example.com', 'password')).toEqual({});
  });

  test('reports both empty fields', () => {
    expect(validateLoginForm('', '')).toEqual({
      email: 'Enter your email address.',
      password: 'Enter your password.',
    });
  });

  test('treats a whitespace-only email as empty', () => {
    expect(validateLoginForm(' \t ', 'password')).toEqual({ email: 'Enter your email address.' });
  });

  test.each(['tenant', 'tenant@example', '@example.com', 'a@@example.com', 'a b@example.com'])(
    'rejects malformed email %s',
    (email) => {
      expect(validateLoginForm(email, 'password')).toEqual({ email: 'Enter a valid email address.' });
    },
  );

  test('trims surrounding email whitespace', () => {
    expect(validateLoginForm(' \ttenant@example.com\n', 'password')).toEqual({});
  });

  test.each([' ', ' password ', 'x'])('does not impose password policy on %j', (password) => {
    expect(validateLoginForm('tenant@example.com', password)).toEqual({});
  });

  test('reports only the missing password when the email is valid', () => {
    expect(validateLoginForm('tenant@example.com', '')).toEqual({ password: 'Enter your password.' });
  });
});
