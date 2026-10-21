"""Auth routes: signup, login, current user, and admin role promotion.

All routes live under /api/v1 to match the client's existing convention.

Security model
--------------
* New users are ALWAYS created with the 'tenant' role (enforced by the DB
  trigger `handle_new_user`). A client cannot self-promote.
* Only an admin (or the service role) can change a role, via `set_user_role`.
* The backend holds the Supabase service role key server-side only; it is never
  sent to the client.
"""

from __future__ import annotations

import httpx
from fastapi import APIRouter, Depends, Header, HTTPException, status

from .config import get_settings
from .schemas import (
    AuthResponse,
    AuthUser,
    LoginRequest,
    MessageResponse,
    SetRoleRequest,
    SignupRequest,
)
from .supabase import (
    DEFAULT_TOKEN_TTL_SECONDS,
    SupabaseAuthError,
    SupabaseNotConfigured,
    get_supabase,
)

router = APIRouter(prefix="/api/v1/auth", tags=["auth"])


def _auth_user_from(auth_user: dict, profile: dict | None) -> AuthUser:
    """Build the client-facing user object from Supabase's user + profile row."""
    metadata = auth_user.get("user_metadata") or {}
    name = (
        (profile or {}).get("full_name")
        or metadata.get("full_name")
        or metadata.get("name")
    )
    role = (profile or {}).get("role") or "tenant"
    return AuthUser(
        id=str(auth_user["id"]),
        email=auth_user.get("email", ""),
        name=name,
        role=role,
    )


@router.post("/signup", response_model=AuthResponse, status_code=status.HTTP_201_CREATED)
async def signup(body: SignupRequest) -> AuthResponse:
    """Create an account. The new user is always a 'tenant'."""
    supabase = get_supabase()
    try:
        result = await supabase.signup(body.email, body.password, body.full_name)
    except SupabaseNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except SupabaseAuthError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc

    user = result.get("user") or {}
    if not user.get("id"):
        raise HTTPException(status_code=502, detail="Supabase did not return a user.")

    profile = await supabase.get_profile(user["id"])
    access_token = result.get("access_token")
    if not access_token:
        # Some Supabase configs require email confirmation before issuing a token.
        raise HTTPException(
            status_code=403,
            detail="Email confirmation is required before signing in.",
        )

    return AuthResponse(
        access_token=access_token,
        expires_in=int(result.get("expires_in") or DEFAULT_TOKEN_TTL_SECONDS),
        user=_auth_user_from(user, profile),
    )


@router.post("/login", response_model=AuthResponse)
async def login(body: LoginRequest) -> AuthResponse:
    """Sign in with email + password."""
    supabase = get_supabase()
    try:
        result = await supabase.login(body.email, body.password)
    except SupabaseNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except SupabaseAuthError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc

    user = result.get("user") or {}
    if not user.get("id") or not result.get("access_token"):
        raise HTTPException(status_code=502, detail="Unexpected Supabase login response.")

    profile = await supabase.get_profile(user["id"])
    return AuthResponse(
        access_token=result["access_token"],
        expires_in=int(result.get("expires_in") or DEFAULT_TOKEN_TTL_SECONDS),
        user=_auth_user_from(user, profile),
    )


@router.get("/me", response_model=AuthUser)
async def me(authorization: str | None = Header(default=None)) -> AuthUser:
    """Return the signed-in user (validates the bearer token with Supabase)."""
    supabase = get_supabase()
    settings = get_settings()

    if not authorization or not authorization.lower().startswith("bearer "):
        raise HTTPException(status_code=401, detail="Missing bearer token.")
    token = authorization.split(" ", 1)[1].strip()

    base_url = settings.supabase_url.rstrip("/")
    if not base_url:
        raise HTTPException(status_code=503, detail="Supabase is not configured.")

    try:
        async with httpx.AsyncClient(timeout=20.0) as client:
            response = await client.get(
                f"{base_url}/auth/v1/user",
                headers={"Authorization": f"Bearer {token}", "apikey": settings.supabase_secret_key},
            )
    except httpx.HTTPError as exc:
        raise HTTPException(status_code=502, detail="Could not reach Supabase.") from exc

    if response.status_code >= 400:
        raise HTTPException(status_code=401, detail="Invalid or expired token.")

    user = response.json()
    profile = await supabase.get_profile(user["id"])
    return _auth_user_from(user, profile)


@router.post("/users/{user_id}/role", response_model=MessageResponse)
async def set_role(user_id: str, body: SetRoleRequest) -> MessageResponse:
    """Promote/demote a user. Only an admin (or service role) may do this.

    The DB function `public.set_user_role` re-checks authorization, so even a
    misconfigured caller cannot change roles.
    """
    supabase = get_supabase()
    try:
        await supabase.set_user_role(user_id, body.role)
    except SupabaseNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except SupabaseAuthError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
    return MessageResponse(detail=f"Role for {user_id} set to {body.role}.")
