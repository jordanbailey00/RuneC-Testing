#include "../../rc-viewer/ui.h"
#include <string.h>

// Context hit tests need dimensions and text metrics, not a graphics context.
static int test_screen_width(void) { return 1280; }
static int test_screen_height(void) { return 720; }
static int drawn_x, drawn_y, drawn_icon, unloaded_icon;
static void test_draw_texture(Texture2D texture, int x, int y, Color tint) {
    (void)tint;
    drawn_x = x; drawn_y = y; drawn_icon = texture.id;
}
static void test_unload_texture(Texture2D texture) { unloaded_icon = texture.id; }
static int missing_icon_drawn;
static void test_text_shadow(const RuneCUiAssets *assets, const char *text,
                              float x, float y, float size, Color color) {
    (void)assets; (void)x; (void)y; (void)size; (void)color;
    missing_icon_drawn = strcmp(text, "?") == 0;
}
static Vector2 test_measure_text(Font font, const char *text, float size, float spacing) {
    (void)font; (void)spacing;
    return (Vector2){strlen(text) * size / 2, size};
}
#define GetScreenWidth test_screen_width
#define GetScreenHeight test_screen_height
#define MeasureTextEx test_measure_text
#define DrawTexture test_draw_texture
#define UnloadTexture test_unload_texture
#define runec_ui_draw_text_shadow test_text_shadow
#include "../../rc-viewer/ui.c"
#include "../../rc-viewer/dev_validation.h"
#include "api.h"
#include "combat.h"
#include "spells.h"
#include <assert.h>
#include <time.h>

#include "../../rc-content/content.h"
#include "items.h"

static void equip(RcWorld *w, int id) {
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = -1};
    w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    assert(rc_inv_add(w->player.inventory, id, 1) == 0);
    assert(rc_item_result_accepted(rc_player_equip(w, 0)));
    rc_world_tick(w);
    assert(w->player.equipment[EQUIP_WEAPON].item_id == id);
}

int main(void) {
    RuneCUiState *ui = calloc(1, sizeof(*ui));
    assert(ui);
    Texture2D icon = {.id = 9001, .width = 36, .height = 32};
    runec_ui_set_item_icon(NULL, 1, icon);
    runec_ui_set_item_icon(ui, 0, icon);
    assert(ui->item_icon_count == 0);
    runec_ui_set_item_icon(ui, 555, icon);
    RuneCUiSlot item = {.item_id = 555, .icon_item_id = 555};
    draw_inventory_item(ui, &item, (Rectangle){100.5f, 200.5f, 32, 32});
    assert(drawn_icon == 9001 && drawn_x == 98 && drawn_y == 200);
    runec_ui_set_item_icon(ui, 555, (Texture2D){0});
    assert(unloaded_icon == 9001 && ui->item_icon_count == 1);
    assert(ui_item_icon_texture(ui, 555)->id == 0 && ui->item_icons[0].ready);
    item.icon_item_id = 0;
    draw_inventory_item(ui, &item, (Rectangle){100, 200, 32, 32});
    assert(missing_icon_drawn);
    runec_ui_set_item_icon(ui, 560, (Texture2D){0});
    assert(ui->item_icon_count == 2 && !ui_item_icon_texture(ui, 999));
    ui->item_icon_count = RUNEC_UI_ITEM_ICON_CACHE;
    runec_ui_set_item_icon(ui, 999, icon);
    assert(ui->item_icon_count == RUNEC_UI_ITEM_ICON_CACHE && unloaded_icon == 9001);
    runec_ui_set_item_icon(ui, 999, (Texture2D){0});
    ui->item_icon_count = 2;
    // Texture metadata tests catalog resolution without creating a GL context.
    for (int i = 0; i < RUNEC_UI_ASSET_MAX; i++) {
        ui->assets.loaded[i] = 1;
        ui->assets.textures[i] = (Texture2D){.id = i + 1, .width = 40, .height = 40};
    }
    ui->current_spellbook = ui->autocast_slot = -1;
    ui->dev_spellbooks_enabled = 1;
    RuneCUiLayout layout;
    ui_layout(1280, 720, &layout);
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_PRAYER;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.spells_path = RC_TEST_SOURCE_DIR "/data/defs/spells.bin";
    cfg.player_actions_path = RC_TEST_SOURCE_DIR "/data/defs/player_actions.bin";
    cfg.prayers_path = RC_TEST_SOURCE_DIR "/data/defs/prayers.bin";
    cfg.varbits_path = RC_TEST_SOURCE_DIR "/data/defs/varbits.bin";
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    rc_content_combat_register(world);
    for (int i = 0; i < SKILL_COUNT; i++)
        world->player.skills.base_level[i] = world->player.skills.boosted_level[i] = 99;
    assert(setenv("RUNEC_DEV_VALIDATION", "1", 1) == 0);
    assert(!runec_dev_validation_seed_prayers(NULL));
    assert(runec_dev_validation_seed_prayers(world));
    assert(rc_prayer_available(world, RC_PRAYER_PIETY) == RC_PRAYER_OK);
    assert(runec_prayer_ui_sync(&ui->prayers, world));
    for (int i = 0; i < 31; i++) {
        const RuneCPrayerUiRef *ref = runec_prayer_ui_ref(i);
        assert(runec_ui_asset(&ui->assets, ref->available_asset));
        assert(runec_ui_asset(&ui->assets, ref->locked_asset));
    }
    assert(!runec_dev_validation_set_spellbook(NULL, 0));
    assert(!runec_dev_validation_set_spellbook(world, -1));
    assert(!runec_dev_validation_set_spellbook(world, 4));
    int counts[] = {76, 27, 46, 46};
    unsigned char mapped[RC_MAX_SPELL_DEFS] = {0};
    int icons = 0;
    for (int choice = 1; choice <= 4; choice++) {
        int book = choice % 4;
        equip(world, book == 1 ? 4675 : book == 3 ? 11791 : 1387);
        ui->active_tab = RUNEC_UI_TAB_CLAN;
        Rectangle button = dev_spellbook_button(&layout, book);
        assert(handle_side_clan_input(ui, &layout, (Vector2){button.x + 10, button.y + 10}));
        assert(ui->last_intent.kind == RUNEC_UI_INTENT_DEV_SPELLBOOK && ui->last_intent.primary == book);
        world->player.selected_spell = world->player.manual_spell_cast = world->player.autocast_spell = 0;
        ui->context_open = 1;
        set_selected_spell_target(ui, 0, "Old spell");
        assert(runec_dev_validation_set_spellbook(world, book));
        rc_world_tick(world);
        assert(runec_ui_sync_spellbook(ui, world));
        assert(world->player.current_spellbook == book && ui->current_spellbook == book);
        assert(world->player.selected_spell == -1 && world->player.autocast_spell == -1);
        assert(!ui->context_open && ui->selected_target.kind == RUNEC_UI_SELECTED_NONE);
        assert(ui->spell_count == counts[book] && ui->spell_scroll == 0);
        ui->active_tab = RUNEC_UI_TAB_SPELLBOOK;
        assert(spell_grid_visible(ui));
        for (int slot = 0; slot < ui->spell_count; slot++) {
            assert(ui->spell_icons[slot] && ui->spell_icons[slot]->width == 40);
            Rectangle cell = spell_grid_cell(ui, &layout, slot);
            assert(cell.x >= layout.side_content.x && cell.x + cell.width <= layout.side_content.x + 190);
            assert(cell.y >= layout.side_content.y && cell.y + cell.height <= layout.side_content.y + 240);
            Vector2 mouse = {cell.x + 2, cell.y + 2};
            assert(handle_spell_click(ui, &layout, mouse, 0));
            assert(ui->last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL && ui->last_intent.primary == slot);
            assert(!strcmp(ui->last_intent.text, ui->spells[slot]->name));
            int spell_id = runec_ui_spell_runtime_id(ui, slot);
            if (spell_id >= 0) {
                mapped[spell_id] = 1;
                rc_player_select_spell(world, spell_id);
                rc_world_tick(world);
                if (world->player.selected_spell != spell_id)
                    fprintf(stderr, "Book %d could not select %s (definition %d, book %d)\n",
                        book, ui->spells[slot]->name, spell_id, rc_spell_def_get(spell_id)->book);
                assert(world->player.selected_spell == spell_id);
            }
            for (int action = 0; action < (ui->spell_can_autocast[slot] ? 3 : 1); action++) {
                assert(handle_spell_click(ui, &layout, mouse, 1));
                RuneCContextMenuLayout box = context_menu_layout_for_ui(ui);
                ui->last_intent = (RuneCUiIntent){0};
                assert(handle_context_click(ui, (Vector2){box.x + 5,
                    box.y + RUNEC_CONTEXT_MENU_HEADER_HEIGHT + action * RUNEC_CONTEXT_MENU_ROW_HEIGHT + 5}));
                assert(ui->last_intent.kind == (action ? RUNEC_UI_INTENT_AUTOCAST_SPELL : RUNEC_UI_INTENT_SELECTED_SPELL));
                assert(!strcmp(ui->last_intent.text, ui->spells[slot]->name));
                assert(ui->last_intent.secondary == (action == 2));
                if (action) {
                    assert(ui->selected_target.kind == RUNEC_UI_SELECTED_NONE);
                    rc_player_set_autocast_spell(world, spell_id, ui->last_intent.secondary);
                    rc_world_tick(world);
                    assert(world->player.autocast_spell == spell_id);
                    assert(world->player.defensive_autocast == (action == 2));
                }
            }
            icons++;
        }
        assert(!handle_spell_click(ui, &layout, (Vector2){0}, 0));
        assert(handle_spell_click(ui, &layout,
            (Vector2){layout.side_content.x + 90, layout.side_content.y + 245}, 0));
        assert(runec_ui_sync_spellbook(ui, world));
        ui->active_tab = RUNEC_UI_TAB_COMBAT;
        RcCombatViewState view;
        rc_combat_get_player_view(world, &view);
        runec_ui_set_combat_style_profile(ui, view.weapon_category);
        assert(!handle_autocast_button(ui, &layout, (Vector2){0}));
        for (int defensive = 0; defensive <= 1; defensive++) {
            Rectangle r = autocast_button(&layout, defensive);
            assert(handle_autocast_button(ui, &layout, (Vector2){r.x + 2, r.y + 2}));
            assert(ui->autocast_picker && ui->autocast_picker_defensive == defensive);
            int slots[RUNEC_UI_SPELL_MAX];
            int n = spell_display_slots(ui, slots);
            if (book == 2) assert(n == 0);
            else {
                assert(n > 0);
                Rectangle cell = spell_grid_cell(ui, &layout, 0);
                assert(handle_spell_click(ui, &layout, (Vector2){cell.x + 2, cell.y + 2}, 0));
                assert(ui->last_intent.kind == RUNEC_UI_INTENT_AUTOCAST_SPELL);
                assert(ui->last_intent.primary == slots[0] && ui->last_intent.secondary == defensive);
                assert(!ui->autocast_picker);
            }
            ui->autocast_picker = 1;
            handle_spell_click(ui, &layout, (Vector2){layout.side_content.x + 140, layout.side_content.y + 245}, 0);
            assert(!ui->autocast_picker);
        }
        ui->autocast_picker = 1;
        handle_spell_click(ui, &layout, (Vector2){layout.side_content.x + 10, layout.side_content.y + 245}, 0);
        assert(ui->last_intent.kind == RUNEC_UI_INTENT_AUTOCAST_SPELL && ui->last_intent.primary == -1);
        rc_player_set_autocast_spell(world, -1, 0);
        rc_combat_set_player_style(world, 1);
        rc_world_tick(world);
        assert(world->player.autocast_spell == -1 && world->player.combat_style != COMBAT_MAGIC);
        for (int i = 0; i < 3; i++) {
            Rectangle melee = combat_style_rect(ui, &layout, i);
            assert(combat_style_at(ui, &layout, (Vector2){melee.x + 2, melee.y + 2}));
            for (int d = 0; d < 2; d++) assert(!CheckCollisionRecs(melee, autocast_button(&layout, d)));
        }
    }
    assert(icons == 195);
    int combat_spells = 0;
    for (int i = 0; rc_spell_def_get(i); i++) {
        const RcSpellDef *spell = rc_spell_def_get(i);
        if (spell->type != RC_SPELL_TYPE_COMBAT || !spell->effect_flags) continue;
        if (!mapped[i]) fprintf(stderr, "Missing combat spell button: %s\n", spell->name);
        assert(mapped[i]);
        combat_spells++;
    }
    assert(combat_spells == 57);
    for (int i = 0; i < 2; i++) {
        equip(world, i ? 11907 : 4151);
        assert(runec_ui_sync_spellbook(ui, world) && !ui->autocast_available);
        assert(!ui->autocast_picker);
        Rectangle melee = combat_style_rect(ui, &layout, 0);
        assert(melee.height == RUNEC_OSRS_COMBAT_STYLES[0].rect.height);
        Rectangle r = autocast_button(&layout, 0);
        assert(!handle_autocast_button(ui, &layout, (Vector2){r.x + 2, r.y + 2}));
    }
    assert(!runec_ui_set_spellbook(NULL, 0));
    assert(!runec_ui_set_spellbook(ui, -1));
    assert(!runec_ui_set_spellbook(ui, 4));
    assert(!runec_ui_sync_spellbook(NULL, world) && !runec_ui_sync_spellbook(ui, NULL));
    assert(runec_ui_spell_runtime_id(NULL, 0) == -1 && runec_ui_spell_runtime_id(ui, -1) == -1);
    ui->active_tab = RUNEC_UI_TAB_SPELLBOOK;
    scroll_spell_grid(ui, 100);
    assert(ui->spell_scroll == 0);
    scroll_spell_grid(ui, -100);
    assert(ui->spell_scroll == 0);
    ui->dev_spellbooks_enabled = 0;
    assert(spell_grid_visible(ui));
    ui->active_tab = RUNEC_UI_TAB_CLAN;
    ui->last_intent.kind = RUNEC_UI_INTENT_NONE;
    Rectangle button = dev_spellbook_button(&layout, 0);
    handle_side_clan_input(ui, &layout, (Vector2){button.x + 5, button.y + 5});
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_NONE);
    assert(setenv("RUNEC_DEV_VALIDATION", "0", 1) == 0);
    assert(!runec_dev_validation_set_spellbook(world, 1));
    assert(unsetenv("RUNEC_DEV_VALIDATION") == 0);
    clock_t start = clock();
    for (int i = 0; i < 10000; i++) assert(runec_ui_set_spellbook(ui, i % 4));
    printf("Spellbook icon projection: %.3f us/switch (10000 switches)\n",
        1000000.0 * (clock() - start) / CLOCKS_PER_SEC / 10000);
    assert(runec_ui_sync_spellbook(ui, world));
    start = clock();
    for (int i = 0; i < 1000000; i++) assert(runec_ui_sync_spellbook(ui, world));
    printf("Cached spellbook sync: %.3f us/frame (1000000 frames)\n",
        1000000.0 * (clock() - start) / CLOCKS_PER_SEC / 1000000);
    rc_world_destroy(world);
    free(ui);
    puts("Spellbook icons, casting, staff picker, defensive autocast, melee and disabled gate passed.");
    return 0;
}
