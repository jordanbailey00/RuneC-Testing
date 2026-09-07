#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "traversal.h"

#define TRAV_PATH RC_TEST_SOURCE_DIR "/data/defs/traversal_edges.bin"
#define BAD_PATH "/tmp/runec_bad_traversal.bin"
#define TRAV_MAGIC 0x56415254u

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

static void write_truncated_row(void) {
    uint32_t header[3] = {TRAV_MAGIC, 1, 1};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(header, sizeof(header), 1, f) == 1);
    fclose(f);
}

static void write_invalid_coordinate_row(void) {
    uint32_t header[3] = {TRAV_MAGIC, 1, 1};
    uint8_t kind = RC_TRAVERSAL_OBJECT;
    uint8_t option = 0;
    uint16_t planes = 0;
    uint32_t source_id = 1;
    uint32_t flags = 0;
    uint16_t coordinates[4] = {1, 1, RC_WORLD_SIZE, 1};
    uint8_t pad[4] = {0};
    uint8_t empty_string = 0;
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(header, sizeof(header), 1, f) == 1);
    assert(fwrite(&kind, sizeof(kind), 1, f) == 1);
    assert(fwrite(&option, sizeof(option), 1, f) == 1);
    assert(fwrite(&planes, sizeof(planes), 1, f) == 1);
    assert(fwrite(&source_id, sizeof(source_id), 1, f) == 1);
    assert(fwrite(&flags, sizeof(flags), 1, f) == 1);
    assert(fwrite(coordinates, sizeof(coordinates), 1, f) == 1);
    assert(fwrite(pad, sizeof(pad), 1, f) == 1);
    assert(fwrite(&empty_string, sizeof(empty_string), 1, f) == 1);
    assert(fwrite(&empty_string, sizeof(empty_string), 1, f) == 1);
    assert(fclose(f) == 0);
}

int main(void) {
    write_bad_header();
    assert(rc_load_traversal_edges(NULL) == -1);
    assert(rc_load_traversal_edges("/missing/traversal_edges.bin") == -1);
    assert(rc_load_traversal_edges(BAD_PATH) == -1);
    write_truncated_row();
    assert(rc_load_traversal_edges(BAD_PATH) == -1);
    write_invalid_coordinate_row();
    assert(rc_load_traversal_edges(BAD_PATH) == -1);
    assert(rc_load_traversal_edges(TRAV_PATH) == 45740);

    int count = 0;
    const RcTraversalEdge *item = rc_traversal_edges_for(RC_TRAVERSAL_ITEM,
                                                         8013, &count);
    assert(item && count > 300);
    assert(item[0].start_x == 0xFFFF);
    assert(item[0].source_semantics == RC_TRAVERSAL_SOURCE_NONE);
    assert(item[0].dest_x > 0 && item[0].dest_y > 0);
    assert(rc_traversal_edges_for(0, 8013, &count) == NULL && count == 0);
    assert(rc_traversal_edges_for(RC_TRAVERSAL_ITEM, 65535, &count) == NULL
           && count == 0);

    const RcTraversalEdge *obj = rc_traversal_find(RC_TRAVERSAL_OBJECT,
                                                   16683, 2465, 3495, 0, 0);
    assert(obj != NULL);
    assert(obj->source_semantics == RC_TRAVERSAL_SOURCE_PLAYER_TILE);
    assert(obj->dest_plane == 1);
    assert(strcmp(obj->action, "Climb-up") == 0);
    assert(rc_traversal_find(RC_TRAVERSAL_OBJECT, 16683, 2465, 3495, 0, 4)
           == NULL);
    assert(rc_traversal_find(RC_TRAVERSAL_OBJECT, 34810, 3185, 3436, 0, 1)
           == NULL);
    assert(rc_traversal_find(RC_TRAVERSAL_OBJECT, 16683,
                             RC_WORLD_SIZE, 3495, 0, 0) == NULL);
    assert(rc_traversal_find(RC_TRAVERSAL_OBJECT, 16683,
                             2465, 3495, RC_MAX_PLANES, 0) == NULL);

    const RcTraversalEdge *dungeon =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, 17384, 3116, 3451, 0, 0);
    assert(dungeon != NULL);
    assert(dungeon->dest_x == 3116);
    assert(dungeon->dest_y == 9851);
    assert(dungeon->dest_plane == 0);

    const RcTraversalEdge *far =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, 398, 3090, 3475, 0, 0);
    assert(far != NULL);
    assert(far->dest_x == 3154);
    assert(far->dest_y == 3924);
    assert(far->dest_plane == 0);

    const RcTraversalEdge *lever =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, 26761, 3090, 3475, 0, 0);
    assert(lever != NULL);
    assert(lever->dest_x == 3153);
    assert(lever->dest_y == 3923);
    assert(lever->dest_plane == 0);

    const RcTraversalEdge *spell =
        rc_traversal_find_target(RC_TRAVERSAL_SPELL, "Falador Teleport");
    assert(spell != NULL);
    assert(spell->kind == RC_TRAVERSAL_SPELL);
    assert(spell->dest_x > 0 && spell->dest_y > 0);
    assert(rc_traversal_find_target(RC_TRAVERSAL_SPELL, "No Such Teleport")
           == NULL);

    int all_count = 0;
    const RcTraversalEdge *all = rc_traversal_edges_all(&all_count);
    const RcTraversalEdge *instance = NULL;
    for (int i = 0; all && i < all_count; i++) {
        if (all[i].flags & 1u) {
            instance = &all[i];
            break;
        }
    }
    assert(instance != NULL);
    assert(instance->source_semantics
           == RC_TRAVERSAL_SOURCE_INSTANCE_TEMPLATE);

    return 0;
}
