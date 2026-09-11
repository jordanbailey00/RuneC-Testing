#ifndef RC_CONTENT_AMMUNITION_H
#define RC_CONTENT_AMMUNITION_H

#include "types.h"
#include "combat_formula.h"

int rc_content_ranged_resource_slot(const RcPlayer *player, const char **failure);
void rc_content_ranged_calc(const RcWorld *world, const RcNpc *target, RcCombatCalc *calc);

#endif
