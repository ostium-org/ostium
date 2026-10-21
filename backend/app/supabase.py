"""Thin async client for the Supabase Auth admin API.

We talk to Supabase Auth directly over HTTP (https://<ref>.supabase.co/auth/v1)
using the service role key. This keeps the backend framework-agnostic and
avoids pulling in the full supabase-py SDK just for auth.

The service role key bypasses RLS, so these calls can create users and read
profiles regardless of the caller's role.
"""

from __future__ import annotations

import httpx

from .config import get_settings

# How long a signed-in access token is valid. Supabase's default JWT expiry is
# 3600s; we surface that to the client as `expires_in`.
DEFAULT_TOKEN_TTL_SECONDS = 3600


class SupabaseNotConfigured(Exception):
    """Raised when a Supabase call is attempted without the required settings."""


class SupabaseAuthError(Exception):
    """Raised when Supabase Auth rejects a request (bad credentials, etc.)."""

    def __init__(self, message: str, status_code: int = 400) -> None:
        super().__init__(message)
        self.message = message
        self.status_code = status_code


class SupabaseClient:
    def __init__(self) -> None:
        settings = get_settings()
        self._base_url = settings.supabase_url.rstrip("/")
        self._secret_key = settings.supabase_secret_key

    def _require_config(self) -> None:
        if not self._base_url or not self._secret_key:
            raise SupabaseNotConfigured(
                "Supabase is not configured. Set SUPABASE_URL and SUPABASE_SECRET_KEY."
            )

    def _headers(self) -> dict[str, str]:
        return {
            "apikey": self._secret_key,
            "Authorization": f"Bearer {self._secret_key}",
            "Content-Type": "application/json",
        }

    async def signup(
        self, email: str, password: str, full_name: str | None = None
    ) -> dict:
        """Create a new auth user. Returns the raw Supabase user object.

        Note: the role is NOT set here. The DB trigger always creates the
        profile as 'tenant'. (We pass full_name through user_metadata so the
        trigger can use it for the profile's display name.)
        """
        self._require_config()
        payload: dict = {"email": email, "password": password}
        if full_name:
            payload["data"] = {"full_name": full_name}

        async with httpx.AsyncClient(timeout=20.0) as client:
            response = await client.post(
                f"{self._base_url}/auth/v1/signup",
                headers=self._headers(),
                json=payload,
            )

        if response.status_code >= 400:
            raise self._error_from_response(response, default_status=400)

        return response.json()

    async def login(self, email: str, password: str) -> dict:
        """Exchange email/password for a session. Returns the raw response.

        Supabase's token endpoint returns { access_token, expires_in, user, ... }.
        """
        self._require_config()
        payload = {"email": email, "password": password}

        async with httpx.AsyncClient(timeout=20.0) as client:
            response = await client.post(
                f"{self._base_url}/auth/v1/token?grant_type=password",
                headers=self._headers(),
                json=payload,
            )

        if response.status_code >= 400:
            raise self._error_from_response(response, default_status=401)

        return response.json()

    async def get_user(self, user_id: str) -> dict:
        """Fetch a single auth user by id (service role)."""
        self._require_config()
        async with httpx.AsyncClient(timeout=20.0) as client:
            response = await client.get(
                f"{self._base_url}/auth/v1/admin/users/{user_id}",
                headers=self._headers(),
            )
        if response.status_code >= 400:
            raise self._error_from_response(response, default_status=404)
        return response.json()

    async def get_profile(self, user_id: str) -> dict | None:
        """Fetch the user's `public.profiles` row (role, full_name, ...).

        Returns the row as a dict, or None if there is no profile. The service
        role bypasses RLS, so this works for any user.
        """
        self._require_config()
        async with httpx.AsyncClient(timeout=20.0) as client:
            response = await client.get(
                f"{self._base_url}/rest/v1/profiles",
                headers=self._headers(),
                params={"id": f"eq.{user_id}", "select": "*"},
            )
        if response.status_code >= 400:
            raise self._error_from_response(response, default_status=404)
        rows = response.json()
        return rows[0] if isinstance(rows, list) and rows else None

    async def set_user_role(self, user_id: str, role: str) -> None:
        """Change a user's role by updating their profile row via the DB.

        We call the `public.set_user_role` Postgres function through the
        Supabase REST endpoint. The function itself enforces that only an
        admin or the service role may change roles.
        """
        self._require_config()
        async with httpx.AsyncClient(timeout=20.0) as client:
            response = await client.post(
                f"{self._base_url}/rest/v1/rpc/set_user_role",
                headers=self._headers(),
                json={"p_user_id": user_id, "p_role": role},
            )
        if response.status_code >= 400:
            raise self._error_from_response(response, default_status=403)

    @staticmethod
    def _error_from_response(response: httpx.Response, default_status: int) -> SupabaseAuthError:
        try:
            body = response.json()
            message = body.get("msg") or body.get("error_description") or body.get("error") or "Supabase auth error"
        except Exception:
            message = response.text or "Supabase auth error"
        # Map common Supabase statuses to something the API layer can surface.
        status = response.status_code if response.status_code in (400, 401, 403, 404, 422) else default_status
        return SupabaseAuthError(str(message), status)


_client: SupabaseClient | None = None


def get_supabase() -> SupabaseClient:
    global _client
    if _client is None:
        _client = SupabaseClient()
    return _client
