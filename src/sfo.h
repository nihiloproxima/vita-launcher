#pragma once

// Lit la chaîne `name` (ex. "TITLE", "APP_VER") d'un param.sfo dans out, sauts de ligne remplacés par des espaces.
// 0 si trouvée, -1 sinon.
int read_sfo_string(const char *path, const char *name, char *out, int out_size);
