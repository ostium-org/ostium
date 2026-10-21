// FORM VALIDATION — login
// These checks exist only to give instant, friendly feedback (typos, empty fields) before a request is
// sent. They are NOT security: the backend decides who gets in. In particular, we don't enforce
// password rules at login, because the server owns the password policy and we shouldn't reveal it.

export type LoginFieldErrors = {
  email?: string;
  password?: string;
};

export type SignupFieldErrors = {
  fullName?: string;
  email?: string;
  password?: string;
  confirmPassword?: string;
};

// Deliberately loose: something@something.something with no spaces. Real validation is the server's job.
const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export function validateLoginForm(
  email: string,
  password: string,
): LoginFieldErrors {
  const errors: LoginFieldErrors = {};

  const trimmedEmail = email.trim();
  if (trimmedEmail === "") {
    errors.email = "Enter your email address.";
  } else if (!EMAIL_PATTERN.test(trimmedEmail)) {
    errors.email = "Enter a valid email address.";
  }

  // Passwords are never trimmed: spaces can be part of one.
  if (password === "") {
    errors.password = "Enter your password.";
  }

  return errors;
}

// Signup is a bit stricter than login: we're creating a credential, so we give
// feedback on the password rules the backend enforces (min 8 chars) and make
// sure the two password fields agree. The backend remains the source of truth.
export function validateSignupForm(
  fullName: string,
  email: string,
  password: string,
  confirmPassword: string,
): SignupFieldErrors {
  const errors: SignupFieldErrors = {};

  const trimmedEmail = email.trim();
  if (trimmedEmail === "") {
    errors.email = "Enter your email address.";
  } else if (!EMAIL_PATTERN.test(trimmedEmail)) {
    errors.email = "Enter a valid email address.";
  }

  if (password === "") {
    errors.password = "Choose a password.";
  } else if (password.length < 8) {
    errors.password = "Use at least 8 characters.";
  }

  if (confirmPassword !== password) {
    errors.confirmPassword = "Passwords don’t match.";
  }

  return errors;
}
