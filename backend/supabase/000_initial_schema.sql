-- ============================================================================
-- Ostium - Supabase / PostgreSQL initial schema
-- ----------------------------------------------------------------------------
-- Run this in the Supabase SQL editor (or via `supabase db push`).
--
-- Design notes (see docs/architecture.md, docs/security.md, docs/api.md):
--   * Auth users live in `auth.users` (Supabase Auth). `profiles` extends them
--     with app-specific info + role. A trigger auto-creates a profile row on
--     signup.
--   * Roles: 'tenant' (default), 'manager' (property manager), 'admin'.
--   * The ESP32-CAM is the enforcement point. Devices authenticate with their
--     own credential (NOT via RLS) and post events using the service role key,
--     which bypasses RLS. RLS below governs the Expo app (tenant/manager) users.
--   * Per docs/security.md, device credentials and the QR signing key are NOT
--     stored in plaintext. `devices.credential_hash` holds a one-way hash used
--     to verify device auth; the QR signing key lives outside the DB entirely.
--   * All tables in the public schema have RLS enabled.
--
-- Idempotency: functions use `create or replace`, tables use `if not exists`,
-- and policies are dropped before being created, so this can be re-run.
-- ============================================================================

-- ----------------------------------------------------------------------------
-- Extensions
-- ----------------------------------------------------------------------------
create extension if not exists "pgcrypto"; -- gen_random_uuid()

-- ============================================================================
-- Helper functions (security definer so they don't recurse through RLS)
-- ============================================================================

create or replace function public.is_admin()
returns boolean
language sql
security definer
stable
set search_path = public
as $$
  select exists (
    select 1
    from public.profiles
    where id = auth.uid()
      and role = 'admin'
  );
$$;

create or replace function public.is_manager()
returns boolean
language sql
security definer
stable
set search_path = public
as $$
  select exists (
    select 1
    from public.profiles
    where id = auth.uid()
      and role in ('manager', 'admin')
  );
$$;

-- True if the current user holds an active, non-expired grant for a unit.
create or replace function public.has_active_unit_access(p_unit_id uuid)
returns boolean
language sql
security definer
stable
set search_path = public
as $$
  select exists (
    select 1
    from public.access_grants g
    where g.unit_id = p_unit_id
      and g.grantee_id = auth.uid()
      and g.is_active
      and (g.valid_until is null or g.valid_until > now())
  );
$$;

-- ============================================================================
-- profiles
-- ============================================================================
create table if not exists public.profiles (
  id          uuid primary key references auth.users (id) on delete cascade,
  full_name   text,
  phone       text,
  role        text not null default 'tenant'
              check (role in ('tenant', 'manager', 'admin')),
  is_active   boolean not null default true,
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now()
);

comment on table public.profiles is
  'App-specific user info and role, extending auth.users.';

-- ============================================================================
-- units
-- ============================================================================
create table if not exists public.units (
  id          uuid primary key default gen_random_uuid(),
  name        text not null,
  address     text,
  building    text,
  floor       text,
  notes       text,
  is_active   boolean not null default true,
  created_by  uuid references public.profiles (id) on delete set null,
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now()
);

comment on table public.units is
  'Apartments / rooms / rental units.';

create index if not exists units_is_active_idx
  on public.units (is_active);

-- ============================================================================
-- devices
-- ============================================================================
create table if not exists public.devices (
  id              uuid primary key default gen_random_uuid(),
  device_key      text not null unique,          -- the {device_id} used in /devices/{device_id}
  name            text,
  unit_id         uuid references public.units (id) on delete set null,
  status          text not null default 'provisioned'
                  check (status in ('provisioned', 'active', 'disabled', 'offline')),
  credential_hash text,                          -- one-way hash of the device credential (never plaintext)
  config          jsonb not null default '{}'::jsonb,
  last_seen_at    timestamptz,
  created_at      timestamptz not null default now(),
  updated_at      timestamptz not null default now()
);

comment on table public.devices is
  'ESP32-CAM door devices. Devices authenticate via credential_hash and post events as the service role.';

create index if not exists devices_unit_id_idx
  on public.devices (unit_id);

-- ============================================================================
-- access_grants
-- ============================================================================
create table if not exists public.access_grants (
  id          uuid primary key default gen_random_uuid(),
  unit_id     uuid not null references public.units (id) on delete cascade,
  grantee_id  uuid not null references public.profiles (id) on delete cascade,
  granted_by  uuid references public.profiles (id) on delete set null,
  grantee_role text not null default 'tenant'
              check (grantee_role in ('tenant', 'guest')),
  valid_from  timestamptz not null default now(),
  valid_until timestamptz,                       -- null = no expiry
  is_active   boolean not null default true,
  notes       text,
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now(),
  unique (unit_id, grantee_id)
);

comment on table public.access_grants is
  'Who is allowed to access which unit, and for how long.';

create index if not exists access_grants_grantee_id_idx
  on public.access_grants (grantee_id);
create index if not exists access_grants_unit_id_idx
  on public.access_grants (unit_id);

-- ============================================================================
-- credentials
-- ============================================================================
create table if not exists public.credentials (
  id          uuid primary key default gen_random_uuid(),
  jti         text not null unique,              -- JWT id / token identifier embedded in the QR
  grant_id    uuid references public.access_grants (id) on delete cascade,
  unit_id     uuid not null references public.units (id) on delete cascade,
  device_id   uuid references public.devices (id) on delete set null, -- null = any device for the unit
  issued_to   uuid not null references public.profiles (id) on delete cascade,
  issued_by   uuid references public.profiles (id) on delete set null,
  issued_at   timestamptz not null default now(),
  expires_at  timestamptz not null,
  is_revoked  boolean not null default false,
  revoked_at  timestamptz,
  revoked_by  uuid references public.profiles (id) on delete set null,
  payload     jsonb not null default '{}'::jsonb, -- signed QR payload / metadata
  created_at  timestamptz not null default now()
);

comment on table public.credentials is
  'Short-lived QR credential records and their revocation state.';

create index if not exists credentials_issued_to_idx
  on public.credentials (issued_to);
create index if not exists credentials_unit_id_idx
  on public.credentials (unit_id);
create index if not exists credentials_expires_at_idx
  on public.credentials (expires_at);

-- ============================================================================
-- access_events
-- ============================================================================
create table if not exists public.access_events (
  id            uuid primary key default gen_random_uuid(),
  device_id     uuid references public.devices (id) on delete set null,
  unit_id       uuid references public.units (id) on delete set null,
  credential_id uuid references public.credentials (id) on delete set null,
  user_id       uuid references public.profiles (id) on delete set null, -- who scanned, if known
  event_type    text not null
                check (event_type in (
                  'access_granted', 'access_denied', 'doorbell',
                  'device_online', 'device_offline', 'config_sync', 'snapshot'
                )),
  result        text check (result in ('granted', 'denied', 'error')),
  reason        text,                            -- e.g. expired, revoked, wrong_unit, unknown_credential
  occurred_at   timestamptz not null default now(), -- device-reported time
  recorded_at   timestamptz not null default now(), -- server time
  metadata      jsonb not null default '{}'::jsonb
);

comment on table public.access_events is
  'Granted/denied scans and device activity (doorbell, online/offline, etc.).';

create index if not exists access_events_unit_id_idx
  on public.access_events (unit_id);
create index if not exists access_events_device_id_idx
  on public.access_events (device_id);
create index if not exists access_events_user_id_idx
  on public.access_events (user_id);
create index if not exists access_events_occurred_at_idx
  on public.access_events (occurred_at desc);

-- ============================================================================
-- Triggers
-- ============================================================================

-- Keep updated_at current.
create or replace function public.set_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

drop trigger if exists profiles_set_updated_at on public.profiles;
create trigger profiles_set_updated_at
  before update on public.profiles
  for each row execute function public.set_updated_at();

drop trigger if exists units_set_updated_at on public.units;
create trigger units_set_updated_at
  before update on public.units
  for each row execute function public.set_updated_at();

drop trigger if exists devices_set_updated_at on public.devices;
create trigger devices_set_updated_at
  before update on public.devices
  for each row execute function public.set_updated_at();

drop trigger if exists access_grants_set_updated_at on public.access_grants;
create trigger access_grants_set_updated_at
  before update on public.access_grants
  for each row execute function public.set_updated_at();

-- Prevent a non-manager from changing their own role (defense in depth;
-- the API should also enforce this).
create or replace function public.prevent_role_change()
returns trigger
language plpgsql
security definer
set search_path = public
as $$
begin
  if new.role is distinct from old.role
     and not public.is_manager() then
    raise exception 'not authorized to change role';
  end if;
  return new;
end;
$$;

drop trigger if exists profiles_prevent_role_change on public.profiles;
create trigger profiles_prevent_role_change
  before update on public.profiles
  for each row execute function public.prevent_role_change();

-- Auto-create a profile row when a new auth user signs up.
create or replace function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.profiles (id, full_name, role)
  values (
    new.id,
    coalesce(
      new.raw_user_meta_data ->> 'full_name',
      new.raw_user_meta_data ->> 'name',
      split_part(coalesce(new.email, ''), '@', 1)
    ),
    coalesce(new.raw_user_meta_data ->> 'role', 'tenant')
  )
  on conflict (id) do nothing;
  return new;
end;
$$;

drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created
  after insert on auth.users
  for each row execute function public.handle_new_user();

-- ============================================================================
-- Row Level Security
-- ============================================================================
alter table public.profiles      enable row level security;
alter table public.units         enable row level security;
alter table public.devices       enable row level security;
alter table public.access_grants enable row level security;
alter table public.credentials   enable row level security;
alter table public.access_events enable row level security;

-- ----------------------------------------------------------------------------
-- profiles
-- ----------------------------------------------------------------------------
drop policy if exists "profiles_select" on public.profiles;
create policy "profiles_select" on public.profiles
  for select
  using (id = auth.uid() or public.is_manager());

drop policy if exists "profiles_insert_own" on public.profiles;
create policy "profiles_insert_own" on public.profiles
  for insert
  with check (id = auth.uid() or public.is_manager());

drop policy if exists "profiles_update" on public.profiles;
create policy "profiles_update" on public.profiles
  for update
  using (id = auth.uid() or public.is_manager())
  with check (id = auth.uid() or public.is_manager());

drop policy if exists "profiles_delete" on public.profiles;
create policy "profiles_delete" on public.profiles
  for delete
  using (public.is_manager());

-- ----------------------------------------------------------------------------
-- units
-- ----------------------------------------------------------------------------
drop policy if exists "units_select" on public.units;
create policy "units_select" on public.units
  for select
  using (
    public.is_manager()
    or public.has_active_unit_access(id)
  );

drop policy if exists "units_insert" on public.units;
create policy "units_insert" on public.units
  for insert
  with check (public.is_manager());

drop policy if exists "units_update" on public.units;
create policy "units_update" on public.units
  for update
  using (public.is_manager())
  with check (public.is_manager());

drop policy if exists "units_delete" on public.units;
create policy "units_delete" on public.units
  for delete
  using (public.is_manager());

-- ----------------------------------------------------------------------------
-- devices
-- ----------------------------------------------------------------------------
drop policy if exists "devices_select" on public.devices;
create policy "devices_select" on public.devices
  for select
  using (
    public.is_manager()
    or (unit_id is not null and public.has_active_unit_access(unit_id))
  );

drop policy if exists "devices_insert" on public.devices;
create policy "devices_insert" on public.devices
  for insert
  with check (public.is_manager());

drop policy if exists "devices_update" on public.devices;
create policy "devices_update" on public.devices
  for update
  using (public.is_manager())
  with check (public.is_manager());

drop policy if exists "devices_delete" on public.devices;
create policy "devices_delete" on public.devices
  for delete
  using (public.is_manager());

-- ----------------------------------------------------------------------------
-- access_grants
-- ----------------------------------------------------------------------------
drop policy if exists "access_grants_select" on public.access_grants;
create policy "access_grants_select" on public.access_grants
  for select
  using (
    public.is_manager()
    or grantee_id = auth.uid()
  );

drop policy if exists "access_grants_insert" on public.access_grants;
create policy "access_grants_insert" on public.access_grants
  for insert
  with check (public.is_manager());

drop policy if exists "access_grants_update" on public.access_grants;
create policy "access_grants_update" on public.access_grants
  for update
  using (public.is_manager())
  with check (public.is_manager());

drop policy if exists "access_grants_delete" on public.access_grants;
create policy "access_grants_delete" on public.access_grants
  for delete
  using (public.is_manager());

-- ----------------------------------------------------------------------------
-- credentials
-- ----------------------------------------------------------------------------
drop policy if exists "credentials_select" on public.credentials;
create policy "credentials_select" on public.credentials
  for select
  using (
    public.is_manager()
    or issued_to = auth.uid()
  );

-- A tenant may mint a credential for a unit they have active access to;
-- managers may issue for anyone.
drop policy if exists "credentials_insert" on public.credentials;
create policy "credentials_insert" on public.credentials
  for insert
  with check (
    public.is_manager()
    or (
      issued_to = auth.uid()
      and public.has_active_unit_access(unit_id)
    )
  );

-- Owners may revoke their own credential; managers may revoke/update any.
drop policy if exists "credentials_update" on public.credentials;
create policy "credentials_update" on public.credentials
  for update
  using (
    public.is_manager()
    or issued_to = auth.uid()
  )
  with check (
    public.is_manager()
    or issued_to = auth.uid()
  );

drop policy if exists "credentials_delete" on public.credentials;
create policy "credentials_delete" on public.credentials
  for delete
  using (public.is_manager());

-- ----------------------------------------------------------------------------
-- access_events
-- ----------------------------------------------------------------------------
drop policy if exists "access_events_select" on public.access_events;
create policy "access_events_select" on public.access_events
  for select
  using (
    public.is_manager()
    or user_id = auth.uid()
    or (unit_id is not null and public.has_active_unit_access(unit_id))
  );

-- Devices post events as the service role (bypasses RLS). Managers may also
-- insert directly (e.g. backfill / testing). Tenants do not insert events.
drop policy if exists "access_events_insert" on public.access_events;
create policy "access_events_insert" on public.access_events
  for insert
  with check (public.is_manager());

drop policy if exists "access_events_delete" on public.access_events;
create policy "access_events_delete" on public.access_events
  for delete
  using (public.is_manager());
