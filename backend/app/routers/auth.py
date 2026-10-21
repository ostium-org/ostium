"""Auth routes: signup, login, current user, and admin role promotion.

All routes live under /api/v1 to match the client's existing convention.

Security model
--------------
* New users are ALWAYS created with the 'tenant' role (enforced by the DB
  trigger `handle_new_user`). A client cannot self-promote.
* Only an active authenticated admin can request a role change.
* The backend holds the Supabase service role key server-side only; it is never
  sent to the client.
"""

from __future__ import annotations

import httpx
from fastapi import APIRouter, Depends, Header, HTTPException, status

from ..config import get_settings
from ..schemas import (
    AuthResponse,
    AuthUser,
    LoginRequest,
    MessageResponse,
    SetRoleRequest,
    SignupRequest,
    SignupConfirmationResponse,
)
from ..supabase import (
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


@router.post("/signup", response_model=AuthResponse | SignupConfirmationResponse, status_code=status.HTTP_201_CREATED)
async def signup(body: SignupRequest) -> AuthResponse | SignupConfirmationResponse:
    """Create an account. The new user is always a 'tenant'."""
    supabase = get_supabase()
    try:
        result = await supabase.signup(body.email, body.password, body.full_name)
    except SupabaseNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except SupabaseAuthError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc

    # Auth REST returns the user directly when email confirmation is required;
    # auto-confirmed signup returns a session with a nested user instead.
    user = result.get("user") or result
    if not user.get("id"):
        raise HTTPException(status_code=502, detail="Supabase did not return a user.")

    access_token = result.get("access_token")
    if not access_token:
        return SignupConfirmationResponse(email=body.email)

    profile = await supabase.get_profile(user["id"])

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


def bearer_token(authorization: str | None) -> str:
    parts = (authorization or "").split()
    if len(parts) != 2 or parts[0].lower() != "bearer":
        raise HTTPException(401, "Missing or invalid bearer token.")
    return parts[1]


@router.get("/me", response_model=AuthUser)
async def me(authorization: str | None = Header(default=None)) -> AuthUser:
    """Validate the bearer token and active profile; also used by user routes."""
    token = bearer_token(authorization)
    settings = get_settings()
    base_url = settings.supabase_url.rstrip("/")
    if not base_url or not settings.supabase_secret_key:
        raise HTTPException(503, "Supabase is not configured.")

    async with httpx.AsyncClient(timeout=20.0) as client:
        response = await client.get(
            f"{base_url}/auth/v1/user",
            headers={"Authorization": f"Bearer {token}", "apikey": settings.supabase_secret_key},
        )
    if response.status_code in (400, 401, 403, 422):
        raise HTTPException(401, "Invalid or expired token.")
    if response.is_error:
        raise HTTPException(502, "User validation unavailable.")
    user = response.json()
    if not user.get("id"):
        raise HTTPException(502, "Invalid user response.")
    profile = await get_supabase().get_profile(user["id"])
    if not profile or profile.get("is_active") is not True:
        raise HTTPException(403, "An active profile is required.")
    return _auth_user_from(user, profile)


@router.post("/users/{user_id}/role", response_model=MessageResponse)
async def set_role(user_id: str, body: SetRoleRequest, user: AuthUser = Depends(me)) -> MessageResponse:
    """Promote/demote a user. Requires an active authenticated admin.

    Caller authorization is enforced before using the service-role RPC.
    """
    if user.role != "admin":
        raise HTTPException(403, "Admin access required.")
    supabase = get_supabase()
    try:
        await supabase.set_user_role(user_id, body.role)
    except SupabaseNotConfigured as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except SupabaseAuthError as exc:
        raise HTTPException(status_code=exc.status_code, detail=exc.message) from exc
    return MessageResponse(detail=f"Role for {user_id} set to {body.role}.")
