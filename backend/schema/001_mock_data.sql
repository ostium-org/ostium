-- ============================================================================
-- Ostium - Mock / example seed data (for quick local testing)
-- ----------------------------------------------------------------------------
-- Run this in the Supabase SQL editor AFTER 000_initial_schema.sql.
--
-- Purpose: give the Expo app a handful of properties (units) plus a couple of
-- linked door devices so the UI has something to show. It does NOT create
-- auth.users / profiles, so access_grants and credentials (which require real
-- signed-in users) are intentionally left out.
--
-- Idempotency: fixed UUIDs + `on conflict ... do nothing`, so this can be
-- re-run safely.
-- ============================================================================

-- ----------------------------------------------------------------------------
-- units (properties / rental units)
-- ----------------------------------------------------------------------------
insert into public.units (id, name, address, building, floor, notes, is_active, created_at, updated_at)
values
  (
    'b1a2c3d4-0000-4000-8000-000000000001',
    'Maple House',
    '12 Maple Street, Dublin 4',
    'Maple House',
    'Ground',
    'Tenant unit with front-door access.',
    true,
    now(),
    now()
  ),
  (
    'b1a2c3d4-0000-4000-8000-000000000002',
    'Harbour View, Unit 3',
    '48 Water Street, Unit 3',
    'Harbour View',
    '1',
    'Waterfront apartment; shared lobby keypad.',
    true,
    now(),
    now()
  ),
  (
    'b1a2c3d4-0000-4000-8000-000000000003',
    'Cedar Lodge',
    '7 Cedar Crescent',
    'Cedar Lodge',
    '2',
    'Detached cottage, rear-door device only.',
    true,
    now(),
    now()
  ),
  (
    'b1a2c3d4-0000-4000-8000-000000000004',
    'Birch Court, Unit 12',
    '103 Birch Avenue, Unit 12',
    'Birch Court',
    '3',
    'Newer build; device still being provisioned.',
    true,
    now(),
    now()
  ),
  (
    'b1a2c3d4-0000-4000-8000-000000000005',
    'Willow Flats, Unit 7',
    '29 Willow Road, Unit 7',
    'Willow Flats',
    '2',
    'Currently inactive (vacated) — keep as a soft-offline example.',
    false,
    now(),
    now()
  )
on conflict (id) do nothing;

-- ----------------------------------------------------------------------------
-- devices (ESP32-CAM door devices, linked to a unit)
-- ----------------------------------------------------------------------------
-- device_key is the {device_id} used in /devices/{device_id}.
-- credential_hash is a placeholder one-way hash (real provisioning computes
-- the true hash server-side); this is mock data for UI/testing only.
insert into public.devices (
  id, device_key, name, unit_id, status, credential_hash, config, last_seen_at, created_at, updated_at
)
values
  (
    'c2b3d4e5-0000-4000-8000-000000000001',
    'esp32-maple-front',
    'Maple House — front door',
    'b1a2c3d4-0000-4000-8000-000000000001',
    'active',
    'mock-hash-9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08',
    '{"threshold_ms": 5000, "report_snippet": false}'::jsonb,
    now() - interval '2 hours',
    now(),
    now()
  ),
  (
    'c2b3d4e5-0000-4000-8000-000000000002',
    'esp32-cedar-rear',
    'Cedar Lodge — rear door',
    'b1a2c3d4-0000-4000-8000-000000000003',
    'offline',
    'mock-hash-6512bd43d9caa6e02c990b0a82652dca4d20dfa0554e0e7d52275e2b554a8e86',
    '{"threshold_ms": 5000, "report_snippet": false}'::jsonb,
    now() - interval '6 days',
    now(),
    now()
  ),
  (
    'c2b3d4e5-0000-4000-8000-000000000003',
    'esp32-birch-lobby',
    'Birch Court — lobby',
    'b1a2c3d4-0000-4000-8000-000000000004',
    'provisioned',
    null,
    '{}'::jsonb,
    null,
    now(),
    now()
  )
on conflict (id) do nothing;

-- ============================================================================
-- Optional: verify what got seeded
-- ============================================================================
-- select u.id, u.name, u.address, u.is_active,
--        d.device_key, d.status
-- from public.units u
-- left join public.devices d on d.unit_id = u.id
-- order by u.name;

-- ----------------------------------------------------------------------------
-- Clean up (if you want to remove the mock rows):
-- ----------------------------------------------------------------------------
-- delete from public.devices where id in (
--   'c2b3d4e5-0000-4000-8000-000000000001',
--   'c2b3d4e5-0000-4000-8000-000000000002',
--   'c2b3d4e5-0000-4000-8000-000000000003'
-- );
-- delete from public.units where id in (
--   'b1a2c3d4-0000-4000-8000-000000000001',
--   'b1a2c3d4-0000-4000-8000-000000000002',
--   'b1a2c3d4-0000-4000-8000-000000000003',
--   'b1a2c3d4-0000-4000-8000-000000000004',
--   'b1a2c3d4-0000-4000-8000-000000000005'
-- );
