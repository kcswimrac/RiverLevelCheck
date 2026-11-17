/**
 * ui_river.h - The single river-forecast screen (466x466 round AMOLED).
 */
#ifndef UI_RIVER_H
#define UI_RIVER_H

#include <stdint.h>

#include "app.h"

void ui_river_create(void (*on_tap)(void));
void ui_river_render(const river_model_t *m, int64_t now);

#endif /* UI_RIVER_H */
