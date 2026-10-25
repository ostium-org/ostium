# Backend schema reference

`schema/000_initial_schema.sql` is the existing shared Supabase schema. This
branch does not modify or reapply it.

The Expo app now uses Supabase Auth directly. HTTP endpoints are implemented in
`supabase/functions/`; see [setup](supabase/README.md). FastAPI is removed.
