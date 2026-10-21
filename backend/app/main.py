"""Ostium backend — FastAPI application entry point.

Run locally:
    uvicorn app.main:app --reload

The app is a thin, framework-agnostic layer over Supabase Auth + Postgres.
It owns user auth (signup/login/me/role) and will later own units, devices,
grants, credentials, and access events.
"""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import get_settings
from .routers import auth

settings = get_settings()

app = FastAPI(
    title="Ostium API",
    version="0.1.0",
    description="Backend for the Ostium door-access app (Supabase-backed).",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origin_list,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(auth.router)


@app.get("/health", tags=["meta"])
async def health() -> dict:
    """Liveness probe. Also reports whether Supabase is configured."""
    return {
        "status": "ok",
        "supabase_configured": bool(settings.supabase_url and settings.supabase_secret_key),
    }
