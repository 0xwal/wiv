#ifndef CONFIG_H
#define CONFIG_H

#include "keymap.h"

int config_load(void);
const KeymapEntry *config_lookup(const char *name);
void config_free(void);
int config_validate(void);

#endif
