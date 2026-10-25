#pragma once

// Lit la clé TITLE d'un param.sfo dans out (sauts de ligne remplacés par des espaces). 0 si trouvée, -1 sinon.
int read_sfo_title(const char *path, char *out, int out_size);
