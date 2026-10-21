import unittest
from unittest.mock import AsyncMock, patch

import httpx

from app.main import app


class SignupTests(unittest.IsolatedAsyncioTestCase):
    async def signup(self, upstream):
        self.supabase = AsyncMock()
        self.supabase.signup.return_value = upstream
        self.supabase.get_profile.return_value = {"role": "tenant"}
        with patch("app.routers.auth.get_supabase", return_value=self.supabase):
            async with httpx.AsyncClient(
                transport=httpx.ASGITransport(app=app), base_url="http://test"
            ) as client:
                return await client.post(
                    "/api/v1/auth/signup",
                    json={"email": "tenant@example.com", "password": "password123"},
                    headers={"Origin": "http://localhost:8081"},
                )

    async def test_confirmation_returns_success_for_direct_user(self):
        response = await self.signup({"id": "user-1", "email": "tenant@example.com"})
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.json(), {
            "confirmation_required": True, "email": "tenant@example.com",
        })
        self.assertIn("access-control-allow-origin", response.headers)
        self.supabase.get_profile.assert_not_awaited()

    async def test_nested_user_without_token_requires_confirmation(self):
        response = await self.signup({"user": {"id": "user-1"}})
        self.assertEqual(response.status_code, 201)
        self.assertTrue(response.json()["confirmation_required"])

    async def test_session_still_signs_in(self):
        response = await self.signup({
            "user": {"id": "user-1", "email": "tenant@example.com"},
            "access_token": "test-token", "expires_in": 3600,
        })
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.json()["access_token"], "test-token")
        self.supabase.get_profile.assert_awaited_once_with("user-1")

    async def test_missing_user_is_still_an_upstream_error(self):
        response = await self.signup({})
        self.assertEqual(response.status_code, 502)
