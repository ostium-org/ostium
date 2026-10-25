"""Record real responses from the FastAPI door routes for the firmware tests.

Runs backend/app in process against an in-memory stand-in for Supabase, the
same way backend/tests/test_access.py does. It drives the door routes the way
the door unit does, sending event bodies in the exact format that
ostium::serializeEvent writes, and records each status and body in
hardware/esp32/test/test_core/backend_responses.h.

The clock and command UUIDs are fixed, so the output only changes when the
backend changes. Each call checks its expected status, and the tool exits
with an error instead of writing the file if any status differs. Run it from
the repository root, with the backend's requirements installed, after any
change to backend/app/routers/access.py or backend/app/routers/auth.py:

    python hardware/esp32/tools/capture_backend_responses.py
"""
import asyncio
import copy
import hashlib
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch
from uuid import UUID

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "backend"))

import httpx  # noqa: E402

from app.config import Settings  # noqa: E402
from app.main import app  # noqa: E402
from app.supabase import SupabaseAuthError  # noqa: E402

OUTPUT = ROOT / "hardware/esp32/test/test_core/backend_responses.h"
DEVICE_KEY = "front"
DEVICE_TOKEN = "device-token"
MANAGER_TOKEN = "manager-token"
T0 = datetime(2026, 10, 22, 15, 0, 0, tzinfo=timezone.utc)
COMMAND_IDS = [
    UUID("11111111-1111-4111-8111-111111111111"),
    UUID("22222222-2222-4222-8222-222222222222"),
    UUID("33333333-3333-4333-8333-333333333333"),
    UUID("55555555-5555-4555-8555-555555555555"),
]
UNKNOWN_COMMAND = "44444444-4444-4444-8444-444444444444"


class Store:
    """The Supabase rows the door routes read and write."""

    def __init__(self):
        self.tables = {
            "devices": [{"id": "5f0c8a2e-0000-4000-8000-000000000001", "device_key": DEVICE_KEY,
                         "unit_id": None, "status": "active",
                         "credential_hash": hashlib.sha256(DEVICE_TOKEN.encode()).hexdigest()}],
            "access_events": [],
        }

    async def get_profile(self, user_id):
        return {"id": user_id, "role": "manager", "is_active": True}

    async def rows(self, table, params):
        def matches(row):
            for key, expected in params.items():
                value = row["metadata"].get(key.split("->>")[1]) if "->>" in key else row.get(key)
                actual = str(value).lower() if isinstance(value, bool) else str(value)
                if actual != expected.removeprefix("eq."):
                    return False
            return True
        return copy.deepcopy([r for r in self.tables[table] if matches(r)])

    async def insert(self, table, row):
        if any(r["id"] == row["id"] for r in self.tables[table]):
            raise SupabaseAuthError("Duplicate record", 409)
        self.tables[table].append(copy.deepcopy(row))


def firmware_timestamp(value):
    """Formats a time the way ostium::formatTimestamp does."""
    return value.strftime("%Y-%m-%dT%H:%M:%S.") + f"{value.microsecond // 1000:03d}Z"


def firmware_event(command, result, reason, occurred_at, action=None):
    """An event body in the exact format ostium::serializeEvent writes."""
    body = {
        "event_id": "evt-" + command["command_id"],
        "command_id": command["command_id"],
        "action": command["action"] if action is None else action,
        "result": result,
        "reason": reason,
        "occurred_at": firmware_timestamp(occurred_at),
    }
    return json.dumps(body, separators=(",", ":"))


async def capture():
    store = Store()
    clock = {"now": T0}
    settings = Settings(supabase_url="https://supabase.test", supabase_secret_key="test-secret")
    factory = httpx.AsyncClient

    async def auth_response(request):
        token = request.headers["authorization"].removeprefix("Bearer ")
        if token != MANAGER_TOKEN:
            return httpx.Response(401, json={"error": "Invalid token"})
        return httpx.Response(200, json={"id": "6a1b2c3d-0000-4000-8000-000000000002",
                                         "email": "manager@example.com"})

    def http_client(**kwargs):
        kwargs.setdefault("transport", httpx.MockTransport(auth_response))
        return factory(**kwargs)

    patches = [
        patch("app.routers.access.get_supabase", return_value=store),
        patch("app.routers.auth.get_supabase", return_value=store),
        patch("app.routers.auth.get_settings", return_value=settings),
        patch("app.routers.access.get_settings", return_value=settings),
        patch("app.routers.auth.httpx.AsyncClient", side_effect=http_client),
        patch("app.routers.access.utcnow", side_effect=lambda: clock["now"]),
        patch("app.routers.access.uuid4", side_effect=COMMAND_IDS),
    ]
    for p in patches:
        p.start()
    captured = {}
    try:
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),
                                     base_url="http://test") as client:
            async def call(name, method, path, token, at, body=None, expect=200):
                clock["now"] = T0 + at
                headers = {"Authorization": f"Bearer {token}"}
                if body is not None:
                    headers["Content-Type"] = "application/json"
                response = await client.request(method, "/api/v1" + path, headers=headers,
                                                content=body)
                if response.status_code != expect:
                    raise SystemExit(f"{name}: expected {expect}, got {response.status_code}: "
                                     f"{response.text}")
                captured[name] = (response.status_code, response.text, body)
                return response

            def ms(value):
                return timedelta(milliseconds=value)

            device = f"/devices/{DEVICE_KEY}"
            command_a = (await call("command_created", "POST", f"/doors/{DEVICE_KEY}/commands",
                                    MANAGER_TOKEN, ms(1000.123),
                                    json.dumps({"action": "unlock"}), expect=201)).json()
            await call("poll_one", "GET", device + "/commands", DEVICE_TOKEN, ms(1200.456))
            command_b = (await call("lock_created", "POST", f"/doors/{DEVICE_KEY}/commands",
                                    MANAGER_TOKEN, ms(1500.789),
                                    json.dumps({"action": "lock"}), expect=201)).json()
            await call("poll_two", "GET", device + "/commands", DEVICE_TOKEN, ms(2000))
            done_a = firmware_event(command_a, "done", None, T0 + ms(1250))
            await call("event_done", "POST", device + "/events", DEVICE_TOKEN, ms(2100), done_a)
            await call("event_repeat", "POST", device + "/events", DEVICE_TOKEN, ms(2200), done_a)
            await call("poll_after_report", "GET", device + "/commands", DEVICE_TOKEN, ms(2500))
            await call("event_lock_done", "POST", device + "/events", DEVICE_TOKEN, ms(2600),
                       firmware_event(command_b, "done", None, T0 + ms(2010)))
            await call("poll_empty", "GET", device + "/commands", DEVICE_TOKEN, ms(3000))
            # A different report under an event ID the backend already holds.
            await call("event_conflict", "POST", device + "/events", DEVICE_TOKEN, ms(3100),
                       firmware_event(command_a, "rejected", "issued_before_boot", T0 + ms(3050)),
                       expect=409)
            command_c = (await call("late_created", "POST", f"/doors/{DEVICE_KEY}/commands",
                                    MANAGER_TOKEN, ms(3200),
                                    json.dumps({"action": "unlock"}), expect=201)).json()
            await call("event_wrong_action", "POST", device + "/events", DEVICE_TOKEN, ms(3300),
                       firmware_event(command_c, "done", None, T0 + ms(3250), action="lock"),
                       expect=409)
            unknown = {"command_id": UNKNOWN_COMMAND, "action": "unlock"}
            await call("event_unknown_command", "POST", device + "/events", DEVICE_TOKEN,
                       ms(3400), firmware_event(unknown, "done", None, T0 + ms(3350)), expect=404)
            await call("event_invalid", "POST", device + "/events", DEVICE_TOKEN, ms(3500),
                       json.dumps({"event_id": "evt-" + command_c["command_id"],
                                   "command_id": command_c["command_id"], "action": None,
                                   "result": "rejected", "reason": "malformed_command",
                                   "occurred_at": firmware_timestamp(T0 + ms(3450))},
                                  separators=(",", ":")), expect=422)
            await call("poll_bad_token", "GET", device + "/commands", "wrong-token", ms(3600),
                       expect=401)
            # A reset that lost an unlock's done event: after the reset, the
            # door rejects the command as issued_before_boot. That's the first
            # report the backend sees, so it's accepted.
            command_d = (await call("reset_created", "POST", f"/doors/{DEVICE_KEY}/commands",
                                    MANAGER_TOKEN, ms(3700),
                                    json.dumps({"action": "unlock"}), expect=201)).json()
            await call("event_after_reset", "POST", device + "/events", DEVICE_TOKEN, ms(4500),
                       firmware_event(command_d, "rejected", "issued_before_boot",
                                      T0 + ms(4450)))
            await call("poll_after_reset_report", "GET", device + "/commands", DEVICE_TOKEN,
                       ms(4600))
            await call("poll_after_expiry", "GET", device + "/commands", DEVICE_TOKEN, ms(14000))
            await call("event_late_rejected", "POST", device + "/events", DEVICE_TOKEN, ms(14100),
                       firmware_event(command_c, "rejected", "expired", T0 + ms(14050)))
    finally:
        for p in reversed(patches):
            p.stop()
    return captured


def raw(text):
    if ')json"' in text:
        raise ValueError("captured text contains the raw string delimiter")
    return f'R"json({text})json"'


def write_header(captured):
    lines = [
        "#pragma once",
        "",
        "// Generated by hardware/esp32/tools/capture_backend_responses.py from the",
        "// FastAPI door routes in backend/app/routers/access.py. Don't edit it by",
        "// hand. Run the tool again when the backend changes.",
        "",
        "namespace backend {",
        "",
        "struct Capture {",
        "  int status;",
        "  const char* body;     // The response body.",
        "  const char* request;  // The request body, or an empty string.",
        "};",
        "",
        f'constexpr const char* kDeviceKey = "{DEVICE_KEY}";',
        f'constexpr const char* kUnlockCommandId = "{COMMAND_IDS[0]}";',
        f'constexpr const char* kLockCommandId = "{COMMAND_IDS[1]}";',
        f'constexpr const char* kLateCommandId = "{COMMAND_IDS[2]}";',
        f'constexpr const char* kResetCommandId = "{COMMAND_IDS[3]}";',
        "",
    ]
    for name, (status, body, request) in captured.items():
        constant = "k" + "".join(part.capitalize() for part in name.split("_"))
        lines.append(f"constexpr Capture {constant} = {{{status}, {raw(body)},")
        lines.append(f"                                {raw(request or '')}}};")
    lines += ["", "}  // namespace backend", ""]
    OUTPUT.write_text("\n".join(lines))


if __name__ == "__main__":
    results = asyncio.run(capture())
    write_header(results)
    for name, (status, body, _) in results.items():
        print(f"{status} {name}: {body[:100]}")
    print(f"Wrote {OUTPUT.relative_to(ROOT)}")
