"""Ostium backend — FastAPI application entry point.

Run locally:
    uvicorn app.main:app --reload

The app is a thin, framework-agnostic layer over Supabase Auth + Postgres.
It owns user auth (signup/login/me/role) and will later own units, devices,
grants, credentials, and access events.
"""

import httpx
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware

from .config import get_settings
from .routers import auth, access
from .supabase import SupabaseAuthError, SupabaseNotConfigured

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
app.include_router(access.router)


@app.exception_handler(SupabaseNotConfigured)
async def missing_config(request: Request, exc: SupabaseNotConfigured):
    return JSONResponse(status_code=503, content={"detail": "Supabase is not configured."})


@app.exception_handler(SupabaseAuthError)
async def upstream_error(request: Request, exc: SupabaseAuthError):
    return JSONResponse(status_code=exc.status_code, content={"detail": exc.message})


@app.exception_handler(httpx.RequestError)
async def connection_error(request: Request, exc: httpx.RequestError):
    code = 504 if isinstance(exc, httpx.TimeoutException) else 502
    return JSONResponse(status_code=code, content={"detail": "Could not reach Supabase."})


@app.get("/health", tags=["meta"])
async def health() -> dict:
    """Liveness probe. Also reports whether Supabase is configured."""
    return {
        "status": "ok",
        "supabase_configured": bool(settings.supabase_url and settings.supabase_secret_key),
    }
