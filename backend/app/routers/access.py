"""Remote door commands, device acknowledgements, and signed QR issuance."""
import base64
import hashlib
import hmac
import json
from datetime import datetime, timedelta, timezone
from typing import Literal
from uuid import UUID, uuid4, uuid5

from cryptography.exceptions import UnsupportedAlgorithm
from cryptography.hazmat.primitives.serialization import load_pem_private_key
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi import APIRouter, Depends, Header, HTTPException
from pydantic import AwareDatetime, BaseModel, ConfigDict, Field

from ..config import get_settings
from ..schemas import AuthUser
from .auth import bearer_token, me
from ..supabase import SupabaseAuthError, get_supabase

router = APIRouter(prefix="/api/v1", tags=["access"])
REPORT_NAMESPACE = UUID("8fc7b31b-f08a-4d13-8b61-899f720f3278")


def utcnow() -> datetime:
    return datetime.now(timezone.utc)


def stamp(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def parse(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)


class CommandRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")
    action: Literal["unlock", "lock"]


class ReportRequest(CommandRequest):
    event_id: str = Field(min_length=1, max_length=200)
    command_id: UUID
    result: Literal["done", "rejected"]
    reason: str | None
    occurred_at: AwareDatetime


async def device_auth(device_id: str, authorization: str | None = Header(default=None)) -> dict:
    token = bearer_token(authorization)
    rows = await get_supabase().rows("devices", {"device_key": f"eq.{device_id}"})
    device = rows[0] if rows else {}
    expected = device.get("credential_hash") or ""
    actual = hashlib.sha256(token.encode()).hexdigest()
    valid = hmac.compare_digest(actual.encode(), expected.encode())
    if not valid or device.get("status") not in ("active", "offline"):
        raise HTTPException(401, "Invalid device credentials.")
    return device


@router.post("/doors/{door_id}/commands", status_code=201)
async def issue_command(door_id: str, body: CommandRequest, user: AuthUser = Depends(me)):
    if user.role not in ("manager", "admin"):
        raise HTTPException(403, "Manager or admin access required.")
    client = get_supabase()
    rows = await client.rows("devices", {"device_key": f"eq.{door_id}"})
    if not rows:
        raise HTTPException(404, "Unknown door.")
    device = rows[0]
    if device["status"] not in ("active", "offline"):
        raise HTTPException(409, "Door device is not active or offline.")
    now = utcnow()
    command = {"command_id": str(uuid4()), "action": body.action, "door_id": door_id,
               "issued_at": stamp(now), "expires_at": stamp(now + timedelta(seconds=10))}
    await client.insert("access_events", {
        "id": command["command_id"], "device_id": device["id"], "unit_id": device.get("unit_id"),
        "user_id": user.id, "event_type": "config_sync", "occurred_at": stamp(now),
        "metadata": {"kind": "door_command", **command},
    })
    return command


def command_data(row: dict) -> dict:
    return {key: row["metadata"][key] for key in
            ("command_id", "action", "door_id", "issued_at", "expires_at")}


@router.get("/devices/{device_id}/commands")
async def poll_commands(device: dict = Depends(device_auth)):
    now = utcnow()
    rows = await get_supabase().rows("access_events", {
        "device_id": f"eq.{device['id']}", "event_type": "eq.config_sync",
    })
    reported = {r["metadata"]["command_id"] for r in rows
                if r["metadata"].get("kind") == "command_result"}
    commands = [command_data(r) for r in rows if r["metadata"].get("kind") == "door_command"
                and r["id"] not in reported and parse(r["metadata"]["expires_at"]) > now]
    commands.sort(key=lambda c: (parse(c["issued_at"]), c["command_id"]))
    return {"server_time": stamp(now), "commands": commands}


@router.post("/devices/{device_id}/events")
async def report_event(body: ReportRequest, device: dict = Depends(device_auth)):
    client = get_supabase()
    report = body.model_dump(mode="json")
    report["occurred_at"] = stamp(body.occurred_at)
    row_id = str(uuid5(REPORT_NAMESPACE, json.dumps([device["id"], body.event_id])))
    metadata = {"kind": "command_result", **report}

    async def check_existing() -> bool:
        existing = await client.rows("access_events", {"id": f"eq.{row_id}"})
        if existing:
            if existing[0]["device_id"] != device["id"] or existing[0]["metadata"] != metadata:
                raise HTTPException(409, "Event ID already used for a different report.")
            return True
        return False

    response = {"event_id": body.event_id, "command_id": str(body.command_id), "recorded": True}
    if await check_existing():
        return response
    commands = await client.rows("access_events", {
        "id": f"eq.{body.command_id}", "device_id": f"eq.{device['id']}",
        "event_type": "eq.config_sync", "metadata->>kind": "eq.door_command",
    })
    if not commands:
        raise HTTPException(404, "Unknown command for this device.")
    if commands[0]["metadata"]["action"] != body.action:
        raise HTTPException(409, "Command action does not match.")
    try:
        await client.insert("access_events", {
            "id": row_id, "device_id": device["id"], "unit_id": device.get("unit_id"),
            "event_type": "config_sync", "occurred_at": report["occurred_at"], "metadata": metadata,
        })
    except SupabaseAuthError as exc:
        if exc.status_code != 409:
            raise
        if not await check_existing():
            raise HTTPException(409, "Report collision; retry delivery.") from exc
    return response


def signed_jwt(claims: dict) -> str:
    settings = get_settings()
    if not settings.qr_signing_private_key or not settings.qr_issuer or not settings.qr_audience or not settings.qr_key_id:
        raise HTTPException(503, "QR signing is not configured.")
    try:
        key = load_pem_private_key(settings.qr_signing_private_key.replace("\\n", "\n").encode(), password=None)
        if not isinstance(key, Ed25519PrivateKey):
            raise ValueError("Wrong key type")
    except (ValueError, TypeError, UnsupportedAlgorithm):
        raise HTTPException(503, "QR signing is not configured correctly.") from None

    def encode(value: bytes) -> str:
        return base64.urlsafe_b64encode(value).rstrip(b"=").decode()

    header = {"alg": "EdDSA", "typ": "JWT", "kid": settings.qr_key_id}
    message = ".".join(encode(json.dumps(part, separators=(",", ":")).encode()) for part in (header, claims))
    return message + "." + encode(key.sign(message.encode()))


@router.post("/houses/{houseId}/qr-credentials", status_code=201)
async def issue_qr(houseId: UUID, user: AuthUser = Depends(me)):
    client = get_supabase()
    unit_id = str(houseId)
    units = await client.rows("units", {"id": f"eq.{unit_id}", "is_active": "eq.true"})
    if not units:
        raise HTTPException(404, "Unknown or inactive unit.")
    now = utcnow()
    expires = now + timedelta(seconds=60)
    grant = None
    if user.role not in ("manager", "admin"):
        grants = await client.rows("access_grants", {
            "unit_id": f"eq.{unit_id}", "grantee_id": f"eq.{user.id}", "is_active": "eq.true",
        })
        grant = next((g for g in grants if parse(g["valid_from"]) <= now and
                      (not g.get("valid_until") or parse(g["valid_until"]) > now)), None)
        if not grant:
            raise HTTPException(403, "No current access grant.")
        if grant.get("valid_until"):
            expires = min(expires, parse(grant["valid_until"]))
    settings = get_settings()
    jti = str(uuid4())
    # NumericDate permits fractional seconds, preserving subsecond grant boundaries.
    claims = {"iss": settings.qr_issuer, "aud": settings.qr_audience, "sub": user.id,
              "unit_id": unit_id, "jti": jti, "iat": now.timestamp(), "exp": expires.timestamp()}
    payload = signed_jwt(claims)
    await client.insert("credentials", {
        "id": jti, "jti": jti, "grant_id": grant["id"] if grant else None,
        "unit_id": unit_id, "issued_to": user.id, "issued_by": user.id,
        "issued_at": stamp(now), "expires_at": stamp(expires), "payload": {"claims": claims, "token": payload},
    })
    return {"house_id": unit_id, "payload": payload, "expires_at": stamp(expires)}
