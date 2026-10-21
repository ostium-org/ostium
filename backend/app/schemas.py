"""Pydantic request/response models for the auth API.

The response shapes intentionally match what the Expo client already parses in
`application/src/services/api.ts` (`parseLoginResponse` / `parseAuthUser`):
  { access_token, expires_in, user: { id, email, name?, role } }
"""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, EmailStr, Field


# --- Requests ---------------------------------------------------------------

class SignupRequest(BaseModel):
    email: EmailStr
    password: str = Field(min_length=8, max_length=72)
    full_name: str | None = Field(default=None, max_length=200)


class LoginRequest(BaseModel):
    email: EmailStr
    password: str


class SetRoleRequest(BaseModel):
    role: str = Field(pattern=r"^(tenant|manager|admin)$")


# --- Responses --------------------------------------------------------------

class AuthUser(BaseModel):
    id: str
    email: str
    name: str | None = None
    role: str  # 'tenant' | 'manager' | 'admin'


class AuthResponse(BaseModel):
    access_token: str
    expires_in: int
    user: AuthUser


class SignupConfirmationResponse(BaseModel):
    confirmation_required: Literal[True] = True
    email: str


class MessageResponse(BaseModel):
    detail: str
