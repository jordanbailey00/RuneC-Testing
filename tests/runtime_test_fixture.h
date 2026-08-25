#ifndef RC_RUNTIME_TEST_FIXTURE_H
#define RC_RUNTIME_TEST_FIXTURE_H

#include "npc.h"
#include "spells.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int rc_test_write(FILE *file, const void *value, size_t size) {
    return file && fwrite(value, size, 1, file) == 1;
}

static int rc_test_write_npc_defs(const char *path,
                                  const RcNpcDef *defs, int count) {
    if (!path || !defs || count <= 0) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    uint32_t header[] = {0x4E444546u, 4u, (uint32_t)count};
    int ok = fwrite(header, sizeof(header[0]), 3, file) == 3;
    for (int i = 0; ok && i < count; i++) {
        const RcNpcDef *def = &defs[i];
        uint32_t id = (uint32_t)def->id;
        uint8_t size = (uint8_t)def->size;
        int16_t combat_level = (int16_t)def->combat_level;
        uint16_t hitpoints = (uint16_t)def->hitpoints;
        uint16_t stats[6];
        int32_t anims[5] = {0};
        for (int j = 0; j < 6; j++) stats[j] = (uint16_t)def->stats[j];
        size_t name_size = strnlen(def->name, sizeof(def->name));
        uint8_t name_len = (uint8_t)name_size;
        uint8_t aggressive = def->aggressive ? 1u : 0u;
        uint16_t max_hit = (uint16_t)def->max_hit;
        uint8_t attack_speed = (uint8_t)def->attack_speed;
        uint8_t aggro_range = (uint8_t)def->aggro_range;
        uint16_t slayer_level = (uint16_t)def->slayer_level;
        uint8_t attack_types = (uint8_t)def->attack_types;
        uint8_t weakness = (uint8_t)def->weakness;
        uint8_t immunities = (def->poison_immune ? 1u : 0u)
                           | (def->venom_immune ? 2u : 0u);
        uint8_t model_count = 0;
        ok = rc_test_write(file, &id, sizeof(id))
          && rc_test_write(file, &size, sizeof(size))
          && rc_test_write(file, &combat_level, sizeof(combat_level))
          && rc_test_write(file, &hitpoints, sizeof(hitpoints))
          && rc_test_write(file, stats, sizeof(stats))
          && rc_test_write(file, anims, sizeof(anims))
          && rc_test_write(file, &name_len, sizeof(name_len))
          && (!name_len || rc_test_write(file, def->name, name_len))
          && rc_test_write(file, &aggressive, sizeof(aggressive))
          && rc_test_write(file, &max_hit, sizeof(max_hit))
          && rc_test_write(file, &attack_speed, sizeof(attack_speed))
          && rc_test_write(file, &aggro_range, sizeof(aggro_range))
          && rc_test_write(file, &slayer_level, sizeof(slayer_level))
          && rc_test_write(file, &attack_types, sizeof(attack_types))
          && rc_test_write(file, &weakness, sizeof(weakness))
          && rc_test_write(file, &immunities, sizeof(immunities))
          && rc_test_write(file, &model_count, sizeof(model_count));
        for (int j = 0; ok && j < RC_NPC_OPTION_COUNT; j++) {
            size_t option_size = strnlen(def->options[j],
                                         sizeof(def->options[j]));
            uint8_t option_len = (uint8_t)option_size;
            ok = rc_test_write(file, &option_len, sizeof(option_len))
              && (!option_len
                  || rc_test_write(file, def->options[j], option_len));
        }
    }
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int rc_test_write_spell_defs(const char *path,
                                    const RcSpellDef *defs, int count) {
    if (!path || !defs || count <= 0) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    uint32_t header[] = {0x4C455053u, 2u, (uint32_t)count};
    int ok = fwrite(header, sizeof(header[0]), 3, file) == 3;
    for (int i = 0; ok && i < count; i++) {
        const RcSpellDef *def = &defs[i];
        size_t name_size = strnlen(def->name, sizeof(def->name));
        uint8_t name_len = (uint8_t)name_size;
        uint8_t rune_count = def->rune_count < RC_SPELL_MAX_RUNES
                           ? def->rune_count : RC_SPELL_MAX_RUNES;
        ok = rc_test_write(file, &name_len, sizeof(name_len))
          && (!name_len || rc_test_write(file, def->name, name_len))
          && rc_test_write(file, &def->book, sizeof(def->book))
          && rc_test_write(file, &def->type, sizeof(def->type))
          && rc_test_write(file, &def->level, sizeof(def->level))
          && rc_test_write(file, &def->slayer_level,
                           sizeof(def->slayer_level))
          && rc_test_write(file, &def->xp_q1, sizeof(def->xp_q1))
          && rc_test_write(file, &def->flags, sizeof(def->flags))
          && rc_test_write(file, &def->max_hit, sizeof(def->max_hit))
          && rc_test_write(file, &def->effect_flags,
                           sizeof(def->effect_flags))
          && rc_test_write(file, &rune_count, sizeof(rune_count));
        for (uint8_t j = 0; ok && j < rune_count; j++) {
            ok = rc_test_write(file, &def->runes[j].item_id,
                               sizeof(def->runes[j].item_id))
              && rc_test_write(file, &def->runes[j].qty,
                               sizeof(def->runes[j].qty));
        }
    }
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static RcWorld *rc_test_world_create_with_defs(RcWorldConfig *config,
                                                const char *tag,
                                                int include_spells) {
    if (!config || !tag || g_npc_def_count <= 0) return NULL;

    char npc_path[256];
    char spell_path[256];
    snprintf(npc_path, sizeof(npc_path), "/tmp/runec_%s_npcs_%ld.bin",
             tag, (long)getpid());
    snprintf(spell_path, sizeof(spell_path), "/tmp/runec_%s_spells_%ld.bin",
             tag, (long)getpid());
    if (!rc_test_write_npc_defs(npc_path, g_npc_defs, g_npc_def_count))
        return NULL;

    config->npc_defs_path = npc_path;
    if (include_spells) {
        if (g_rc_spell_count <= 0
                || !rc_test_write_spell_defs(spell_path, g_rc_spell_defs,
                                             g_rc_spell_count)) {
            unlink(npc_path);
            return NULL;
        }
        config->spells_path = spell_path;
    }

    RcWorld *world = rc_world_create_config(config);
    unlink(npc_path);
    if (include_spells) unlink(spell_path);
    return world;
}

#endif
