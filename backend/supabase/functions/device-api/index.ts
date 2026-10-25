import { createHandler } from "../_shared/api.ts";
import { environment } from "../_shared/rest.ts";

const { store, settings } = environment();
Deno.serve(createHandler("device", store, settings));
