"""API integration tests with an in-memory stand-in for existing Supabase rows."""
import asyncio
import base64
import copy
import hashlib
import json
import unittest
from datetime import datetime, timedelta, timezone
from unittest.mock import patch
from uuid import uuid4

import httpx
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from app.config import Settings
from app.main import app
from app.supabase import SupabaseAuthError, SupabaseClient, SupabaseNotConfigured

NOW = datetime(2026, 10, 22, 12, 0, 0, 123456, tzinfo=timezone.utc)
UNIT = str(uuid4())
USER = str(uuid4())
DEVICE = str(uuid4())


class Store:
    def __init__(self):
        self.profile = {"id": USER, "role": "manager", "is_active": True}
        self.tables = {
            "devices": [{"id": DEVICE, "device_key": "front", "unit_id": UNIT, "status": "active",
                         "credential_hash": hashlib.sha256(b"device-token").hexdigest()}],
            "units": [{"id": UNIT, "is_active": True}], "access_events": [],
            "access_grants": [], "credentials": [],
        }
        self.fail_write = False
        self.role_changes = []

    async def validate_token(self, token):
        if token != "user-token":
            raise SupabaseAuthError("Invalid token", 401)
        return {"id": USER, "email": "manager@example.com"}

    async def get_profile(self, user_id):
        return self.profile

    async def set_user_role(self, user_id, role):
        self.role_changes.append((user_id, role))

    async def rows(self, table, params):
        def matches(row):
            for key, expected in params.items():
                value = row["metadata"].get(key.split("->>")[1]) if "->>" in key else row.get(key)
                actual = str(value).lower() if isinstance(value, bool) else str(value)
                if actual != expected.removeprefix("eq."):
                    return False
            return True
        # Yield before returning a snapshot so simultaneous retries can race on INSERT.
        result = copy.deepcopy([r for r in self.tables[table] if matches(r)])
        await asyncio.sleep(0)
        return result

    async def insert(self, table, row):
        await asyncio.sleep(0)
        if self.fail_write:
            raise SupabaseAuthError("Database write failed", 502)
        if any(r["id"] == row["id"] for r in self.tables[table]):
            raise SupabaseAuthError("Duplicate record", 409)
        self.tables[table].append(copy.deepcopy(row))


class AccessTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.store = Store()
        self.key = Ed25519PrivateKey.generate()
        pem = self.key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                     serialization.NoEncryption()).decode()
        self.settings = Settings(supabase_url="https://supabase.test", supabase_secret_key="test-secret",
                                 qr_signing_private_key=pem)
        factory = httpx.AsyncClient

        async def auth_response(request):
            self.assertEqual(request.url.path, "/auth/v1/user")
            self.assertEqual(request.headers["apikey"], "test-secret")
            token = request.headers["authorization"].removeprefix("Bearer ")
            try:
                user = await self.store.validate_token(token)
            except SupabaseAuthError as exc:
                return httpx.Response(exc.status_code, json={"error": exc.message})
            return httpx.Response(200, json=user)

        def http_client(**kwargs):
            kwargs.setdefault("transport", httpx.MockTransport(auth_response))
            return factory(**kwargs)
        self.patches = [
            patch("app.routers.access.get_supabase", return_value=self.store),
            patch("app.routers.auth.get_supabase", return_value=self.store),
            patch("app.routers.auth.get_settings", return_value=self.settings),
            patch("app.routers.auth.httpx.AsyncClient", side_effect=http_client),
            patch("app.routers.access.utcnow", return_value=NOW),
            patch("app.routers.access.get_settings", return_value=self.settings),
        ]
        for p in self.patches:
            p.start()
            self.addCleanup(p.stop)
        self.client = httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test")
        self.addAsyncCleanup(self.client.aclose)

    async def request(self, method, path, token="user-token", **kwargs):
        headers = {"Authorization": f"Bearer {token}"} if token is not None else {}
        return await self.client.request(method, "/api/v1" + path, headers=headers, **kwargs)

    async def issue(self, action="unlock"):
        response = await self.request("POST", "/doors/front/commands", json={"action": action})
        self.assertEqual(response.status_code, 201, response.text)
        return response.json()

    async def poll(self, token="device-token", device="front"):
        return await self.request("GET", f"/devices/{device}/commands", token)

    def report(self, command, **overrides):
        return {"event_id": "event-1", "command_id": command["command_id"], "action": command["action"],
                "result": "done", "reason": None, "occurred_at": "2026-10-22T09:00:00-03:00", **overrides}

    async def post_report(self, report):
        return await self.request("POST", "/devices/front/events", "device-token", json=report)

    async def test_manager_and_admin_issue_ten_second_commands(self):
        for role in ("manager", "admin"):
            self.store.profile["role"] = role
            command = await self.issue("lock")
            self.assertEqual(command["door_id"], "front")
            self.assertEqual(datetime.fromisoformat(command["expires_at"]) - datetime.fromisoformat(command["issued_at"]), timedelta(seconds=10))
        self.assertEqual(len(self.store.tables["access_events"]), 2)

    async def test_user_authorization_and_role_change(self):
        for token in (None, "invalid"):
            r = await self.request("POST", "/doors/front/commands", token, json={"action": "lock"})
            self.assertEqual(r.status_code, 401)
        self.store.profile["role"] = "tenant"
        r = await self.request("POST", "/doors/front/commands", json={"action": "unlock"})
        self.assertEqual(r.status_code, 403)
        for role in ("tenant", "manager"):
            self.store.profile["role"] = role
            r = await self.request("POST", f"/auth/users/{USER}/role", json={"role": "admin"})
            self.assertEqual(r.status_code, 403)
        self.assertEqual(self.store.role_changes, [])
        r = await self.request("POST", f"/auth/users/{USER}/role", None, json={"role": "admin"})
        self.assertEqual(r.status_code, 401)
        self.store.profile["role"] = "admin"
        r = await self.request("POST", f"/auth/users/{USER}/role", json={"role": "tenant"})
        self.assertEqual(r.status_code, 200)
        self.store.profile["is_active"] = False
        for path in ("/auth/me", f"/houses/{UNIT}/qr-credentials"):
            r = await self.request("GET" if path == "/auth/me" else "POST", path)
            self.assertEqual(r.status_code, 403)
        self.store.profile = None
        self.assertEqual((await self.request("GET", "/auth/me")).status_code, 403)

    async def test_me_uses_validated_profile(self):
        r = await self.request("GET", "/auth/me")
        self.assertEqual(r.status_code, 200)
        self.assertEqual(r.json()["role"], "manager")

    async def test_unknown_disabled_and_offline_devices(self):
        r = await self.request("POST", "/doors/missing/commands", json={"action": "lock"})
        self.assertEqual(r.status_code, 404)
        for status in ("disabled", "provisioned"):
            self.store.tables["devices"][0]["status"] = status
            r = await self.request("POST", "/doors/front/commands", json={"action": "lock"})
            self.assertEqual(r.status_code, 409)
            self.assertEqual((await self.poll()).status_code, 401)
            self.assertEqual((await self.post_report(self.report({"command_id": str(uuid4()), "action": "lock"}))).status_code, 401)
        self.store.tables["devices"][0]["status"] = "offline"
        await self.issue()
        self.assertEqual((await self.poll()).status_code, 200)
        for token in (None, "wrong", "user-token"):
            self.assertEqual((await self.poll(token)).status_code, 401)
        self.assertEqual((await self.poll(device="unknown")).status_code, 401)

    async def test_command_validation(self):
        for body in ({"action": "open"}, {"action": "unlock", "extra": 1}, {}):
            r = await self.request("POST", "/doors/front/commands", json=body)
            self.assertEqual(r.status_code, 422)

    async def test_polling_order_repeat_expiry_and_device_isolation(self):
        first, second = await self.issue(), await self.issue("lock")
        expected = sorted([first, second], key=lambda c: c["command_id"])
        for _ in range(2):
            self.assertEqual((await self.poll()).json()["commands"], expected)
        with patch("app.routers.access.utcnow", return_value=NOW + timedelta(seconds=10)):
            self.assertEqual((await self.poll()).json()["commands"], [])
        with patch("app.routers.access.utcnow", return_value=NOW + timedelta(seconds=9, microseconds=999999)):
            self.assertEqual(len((await self.poll()).json()["commands"]), 2)
        self.store.tables["devices"].append({**self.store.tables["devices"][0], "id": str(uuid4()), "device_key": "back"})
        self.assertEqual((await self.poll(device="back")).json()["commands"], [])
        r = await self.request("POST", "/devices/back/events", "device-token", json=self.report(first))
        self.assertEqual(r.status_code, 404)
        with patch("app.routers.access.utcnow", return_value=NOW + timedelta(seconds=1)):
            later = await self.issue()
        self.assertEqual((await self.poll()).json()["commands"], expected + [later])

    async def test_acknowledgements_and_late_reports(self):
        for result in ("done", "rejected"):
            command = await self.issue()
            report = self.report(command, event_id=result, result=result)
            with patch("app.routers.access.utcnow", return_value=NOW + timedelta(days=1)):
                self.assertEqual((await self.post_report(report)).status_code, 200)
            self.assertEqual((await self.poll()).json()["commands"], [])
            r = await self.post_report({**report, "occurred_at": "2026-10-22T12:00:00Z"})
            self.assertEqual(r.status_code, 200)
        self.assertEqual(len(self.store.tables["access_events"]), 4)

    async def test_concurrent_retries_and_conflicts(self):
        command = await self.issue()
        report = self.report(command)
        responses = await asyncio.gather(*(self.post_report(report) for _ in range(8)))
        self.assertTrue(all(r.status_code == 200 for r in responses))
        self.assertEqual(len(self.store.tables["access_events"]), 2)
        for field, value in (("result", "rejected"), ("reason", "jammed"), ("action", "lock"),
                             ("command_id", str(uuid4())), ("occurred_at", "2026-10-22T12:00:01Z")):
            r = await self.post_report({**report, field: value})
            self.assertEqual(r.status_code, 409)
        r = await self.post_report(self.report(command, event_id="new", action="lock"))
        self.assertEqual(r.status_code, 409)
        r = await self.post_report(self.report(command, event_id="new", command_id=str(uuid4())))
        self.assertEqual(r.status_code, 404)
        r = await self.post_report(self.report(command, occurred_at="2026-10-22T12:00:00"))
        self.assertEqual(r.status_code, 422)

    async def test_device_tokens_and_event_ids_are_scoped(self):
        other_id = str(uuid4())
        self.store.tables["devices"].append({
            **self.store.tables["devices"][0], "id": other_id, "device_key": "back",
            "credential_hash": hashlib.sha256(b"other-token").hexdigest(),
        })
        first = await self.issue()
        r = await self.request("POST", "/doors/back/commands", json={"action": "unlock"})
        self.assertEqual(r.status_code, 201)
        second = r.json()
        self.assertEqual((await self.poll(device="back")).status_code, 401)
        self.assertEqual((await self.poll(token="other-token")).status_code, 401)
        self.assertEqual((await self.poll(token="other-token", device="back")).json()["commands"], [second])
        self.assertEqual((await self.post_report(self.report(first))).status_code, 200)
        r = await self.request("POST", "/devices/back/events", "other-token", json=self.report(second))
        self.assertEqual(r.status_code, 200)
        self.assertEqual(len(self.store.tables["access_events"]), 4)

    async def test_concurrent_conflicting_reports(self):
        command = await self.issue()
        first = self.report(command)
        second = self.report(command, reason="different")
        responses = await asyncio.gather(self.post_report(first), self.post_report(second))
        self.assertEqual(sorted(r.status_code for r in responses), [200, 409])
        self.assertEqual(len(self.store.tables["access_events"]), 2)

    async def qr(self):
        return await self.request("POST", f"/houses/{UNIT}/qr-credentials")

    def grant(self, **overrides):
        self.store.profile["role"] = "tenant"
        grant = {"id": str(uuid4()), "unit_id": UNIT, "grantee_id": USER, "is_active": True,
                 "valid_from": NOW.isoformat(), "valid_until": None, **overrides}
        self.store.tables["access_grants"] = [grant]
        return grant

    async def test_qr_signature_claims_and_persistence(self):
        r = await self.qr()
        self.assertEqual(r.status_code, 201, r.text)
        response = r.json()
        parts = response["payload"].split(".")
        decode = lambda s: base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))
        self.key.public_key().verify(decode(parts[2]), ".".join(parts[:2]).encode())
        header, claims = json.loads(decode(parts[0])), json.loads(decode(parts[1]))
        self.assertEqual(header, {"alg": "EdDSA", "typ": "JWT", "kid": self.settings.qr_key_id})
        self.assertEqual(claims["iss"], self.settings.qr_issuer)
        self.assertEqual(claims["aud"], self.settings.qr_audience)
        self.assertEqual(claims["sub"], USER)
        self.assertEqual(claims["unit_id"], UNIT)
        self.assertEqual(claims["iat"], NOW.timestamp())
        self.assertEqual(claims["exp"], (NOW + timedelta(seconds=60)).timestamp())
        row = self.store.tables["credentials"][0]
        self.assertEqual(row["jti"], claims["jti"])
        self.assertEqual(row["payload"]["token"], response["payload"])
        self.assertEqual(row["expires_at"], response["expires_at"])
        self.assertEqual(response["house_id"], UNIT)
        self.assertNotEqual((await self.qr()).json()["payload"], response["payload"])

    async def test_grant_boundaries(self):
        self.store.profile["role"] = "tenant"
        self.assertEqual((await self.qr()).status_code, 403)
        for fields in ({"valid_from": (NOW + timedelta(microseconds=1)).isoformat()},
                       {"valid_until": NOW.isoformat()}, {"is_active": False},
                       {"grantee_id": str(uuid4())}, {"unit_id": str(uuid4())}):
            self.grant(**fields)
            self.assertEqual((await self.qr()).status_code, 403)
        for duration in (0.000001, 15, 120):
            grant = self.grant(valid_until=(NOW + timedelta(seconds=duration)).isoformat())
            r = await self.qr()
            self.assertEqual(r.status_code, 201)
            self.assertEqual(datetime.fromisoformat(r.json()["expires_at"]), NOW + timedelta(seconds=min(60, duration)))
            self.assertEqual(self.store.tables["credentials"][-1]["grant_id"], grant["id"])
        self.grant()
        self.assertEqual((await self.qr()).status_code, 201)
        self.store.tables["units"][0]["is_active"] = False
        self.assertEqual((await self.qr()).status_code, 404)
        self.store.tables["units"] = []
        self.assertEqual((await self.qr()).status_code, 404)

    async def test_missing_and_invalid_key_and_persistence_failure(self):
        for key in ("", "bad PEM"):
            self.settings.qr_signing_private_key = key
            self.assertEqual((await self.qr()).status_code, 503)
        self.assertEqual(self.store.tables["credentials"], [])
        self.settings.qr_signing_private_key = self.key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()).decode()
        self.store.fail_write = True
        self.assertEqual((await self.qr()).status_code, 502)
        self.assertEqual(self.store.tables["credentials"], [])

    async def test_controlled_transport_failures(self):
        with patch.object(self.store, "validate_token", side_effect=SupabaseNotConfigured()):
            self.assertEqual((await self.request("GET", "/auth/me")).status_code, 503)
        for error, code in ((httpx.ConnectError("private upstream details"), 502),
                            (httpx.ReadTimeout("private upstream details"), 504)):
            with patch.object(self.store, "validate_token", side_effect=error):
                r = await self.request("GET", "/auth/me")
                self.assertEqual(r.status_code, code)
                self.assertNotIn("private", r.text)


class SupabaseTransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_queries_pagination_and_insert_collision(self):
        client = SupabaseClient()
        client._base_url = "https://example.test"
        client._secret_key = "server-secret"
        requests = []

        def handler(request):
            requests.append(request)
            self.assertEqual(request.headers["apikey"], "server-secret")
            if request.method == "POST":
                return httpx.Response(409, json={"code": "23505"})
            return httpx.Response(200, json=[{"id": "row"}] if request.url.params["offset"] == "0" else [])

        factory = httpx.AsyncClient
        with patch("app.supabase.httpx.AsyncClient", side_effect=lambda **kw: factory(transport=httpx.MockTransport(handler), **kw)):
            rows = await client.rows("access_events", {"device_id": f"eq.{DEVICE}", "metadata->>kind": "eq.door_command"})
            self.assertEqual(rows, [{"id": "row"}])
            self.assertEqual(requests[1].url.params["offset"], "1")
            self.assertEqual(requests[0].url.params["metadata->>kind"], "eq.door_command")
            with self.assertRaises(SupabaseAuthError) as cm:
                await client.insert("access_events", {"id": "row"})
            self.assertEqual(cm.exception.status_code, 409)
