#include "drops.h"

#include <assert.h>
#include <stdio.h>

// Synthetic malformed bytes are a loose-loader unit test; installed assets
// are checked separately by test_drops_runtime in loose and forced-pack mode.
int main(void) {
    const char *path = "/tmp/runec_drop_rejection_payload.bin";
    RcDropData data;
    rc_drop_data_init(&data);
    for (int kind = 0; kind < 9; kind++) {
        FILE *f = fopen(path, "wb");
        assert(f);
        uint32_t head[] = {0x504F5244u, kind == 0 ? 1u : 2u, 1, 414,
                          kind == 1 ? 4u : RC_DROP_UNPUBLISHED_RATE};
        size_t words = kind == 6 ? 4 : 5;  // Truncated rejection field.
        assert(fwrite(head, sizeof(head[0]), words, f) == words);
        if (kind != 6) {
            for (int group = 0; group < 3; group++) {
                int has_reward = kind == group + 2;
                fputc(has_reward, f);
                if (has_reward) {
                    uint32_t item = 995, rarity = 1;
                    uint16_t quantity[] = {1, 1};
                    assert(fwrite(&item, sizeof(item), 1, f) == 1);
                    assert(fwrite(quantity, sizeof(quantity), 1, f) == 1);
                    if (group) assert(fwrite(&rarity, sizeof(rarity), 1, f) == 1);
                }
            }
            uint32_t rare = kind == 5 ? 1 : 0;
            assert(fwrite(&rare, sizeof(rare), 1, f) == 1);
            if (kind == 7) fputc(42, f);
        }
        fclose(f);
        int result = rc_load_drops_into(path, &data);
        assert(result == (kind == 8 ? 1 : -1));
        if (kind != 8) assert(!data.tables && !data.entries && !data.table_count);
    }
    assert(data.tables[0].rejection_flags == RC_DROP_UNPUBLISHED_RATE);
    rc_drop_data_free(&data);
    puts("test_drop_rejection_payload: malformed/reward-bearing rejections fail; valid empty rejection loads");
    return 0;
}
