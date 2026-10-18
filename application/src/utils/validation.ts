// FORM VALIDATION — login
// These checks exist only to give instant, friendly feedback (typos, empty fields) before a request is
// sent. They are NOT security: the backend decides who gets in. In particular, we don't enforce
// password rules at login, because the server owns the password policy and we shouldn't reveal it.

export type LoginFieldErrors = {
  email?: string;
  password?: string;
};

// Deliberately loose: something@something.something with no spaces. Real validation is the server's job.
const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export function validateLoginForm(email: string, password: string): LoginFieldErrors {
  const errors: LoginFieldErrors = {};

  const trimmedEmail = email.trim();
  if (trimmedEmail === '') {
    errors.email = 'Enter your email address.';
  } else if (!EMAIL_PATTERN.test(trimmedEmail)) {
    errors.email = 'Enter a valid email address.';
  }

  // Passwords are never trimmed: spaces can be part of one.
  if (password === '') {
    errors.password = 'Enter your password.';
  }

  return errors;
}