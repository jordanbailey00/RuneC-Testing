#include "spells.h"
#include "game_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_fixture(const char *path, const RcSpellDef *s, int version, int rows) {
    FILE *f = fopen(path, "wb");
    assert(f);
    uint32_t header[] = {0x4c455053, version, rows};
    assert(fwrite(header, sizeof(header), 1, f) == 1);
    for (int i = 0; i < rows; i++) {
        size_t len = strlen(s->name);
        assert(fputc(len, f) != EOF);
        assert(fwrite(s->name, 1, len, f) == len);
        assert(fputc(s->book, f) != EOF);
        assert(fputc(s->type, f) != EOF);
        assert(fputc(s->level, f) != EOF);
        assert(fputc(s->slayer_level, f) != EOF);
        assert(fwrite(&s->xp_q1, 2, 1, f) == 1);
        assert(fputc(s->flags, f) != EOF);
        if (version == 2) {
            assert(fwrite(&s->max_hit, 2, 1, f) == 1);
            assert(fwrite(&s->effect_flags, 2, 1, f) == 1);
        }
        assert(fputc(s->rune_count, f) != EOF);
        for (int r = 0; r < s->rune_count; r++) {
            const RcSpellRune *rune = &s->runes[r % RC_SPELL_MAX_RUNES];
            assert(fwrite(&rune->item_id, 4, 1, f) == 1);
            assert(fputc(rune->qty, f) != EOF);
        }
    }
    assert(fclose(f) == 0);
}

int main(void) {
    char path[] = "/tmp/runec_spells_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    RcSpellDef valid = {.name = "Test spell", .book = 0, .type = 1, .level = 1,
        .xp_q1 = 55, .max_hit = 2, .effect_flags = 1, .rune_count = 1, .runes = {{556, 1}}};
    RcSpellDef defs[2] = {{0}}, before[2];
    int count = 0;
    for (int version = 1; version <= 2; version++) {
        write_fixture(path, &valid, version, 1);
        assert(rc_load_spells_into(path, defs, 2, &count) == 1);
        assert(count == 1 && defs[0].loaded && defs[0].xp_q1 == 55);
        assert(defs[0].max_hit == (version == 2 ? 2 : 0));
    }
    memcpy(before, defs, sizeof(defs));
    FILE *complete = fopen(path, "rb");
    assert(complete && fseek(complete, 0, SEEK_END) == 0);
    long length = ftell(complete);
    assert(length > 0 && fclose(complete) == 0);
    for (long bytes = 0; bytes < length; bytes++) {
        write_fixture(path, &valid, 2, 1);
        assert(truncate(path, bytes) == 0);
        assert(rc_load_spells_into(path, defs, 2, &count) == -1);
        assert(count == 1 && !memcmp(before, defs, sizeof(defs)));
    }
    for (int test = 0; test < 14; test++) {
        RcSpellDef bad = valid;
        int rows = 1;
        switch (test) {
        case 0: bad.rune_count = 9; break;
        case 1: bad.runes[0].qty = 0; break;
        case 2: bad.runes[0].item_id = 0; break;
        case 3: bad.rune_count = 2; bad.runes[1] = bad.runes[0]; break;
        case 4: bad.book = 5; break;
        case 5: bad.type = 8; break;
        case 6: bad.level = 0; break;
        case 7: bad.slayer_level = 100; break;
        case 8: bad.flags = 2; break;
        case 9: bad.effect_flags = 64; break;
        case 10: bad.name[0] = 0; break;
        case 11: rows = 2; break;
        }
        write_fixture(path, &bad, 2, rows);
        if (test == 12) {
            FILE *f = fopen(path, "ab");
            assert(f && fputc(0, f) != EOF && fclose(f) == 0);
        }
        if (test == 13) assert(truncate(path, 15) == 0);
        assert(rc_load_spells_into(path, defs, 2, &count) == -1);
        assert(count == 1 && !memcmp(before, defs, sizeof(defs)));
    }
    write_fixture(path, &valid, 2, 3);
    assert(rc_load_spells_into(path, defs, 2, &count) == -1);
    assert(count == 1 && !memcmp(before, defs, sizeof(defs)));
    valid.runes[0].item_id = INT32_MAX;
    write_fixture(path, &valid, 2, 1);
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.spells_path = path;
    RcGameDataLoadReport report;
    assert(!rc_game_data_load(&cfg, &report));
    assert(strstr(report.message, "missing or invalid rune"));
    unlink(path);
    puts("spells: legacy/v2 loading and explicit atomic rejection passed");
    return 0;
}
