#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "streaming.h"

int main(void) {
    RcWorldStreamingConfig backend = rc_world_streaming_config_default();
    assert(backend.active_radius_regions == 2);
    assert(backend.preload_radius_regions == 3);
    assert(backend.max_cached_regions == 64);

    backend.active_radius_regions = 4;
    backend.preload_radius_regions = 1;
    backend.max_cached_regions = 10;
    rc_world_streaming_config_sanitize(&backend);
    assert(backend.active_radius_regions == 4);
    assert(backend.preload_radius_regions == 4);
    assert(backend.max_cached_regions == 81);
    rc_world_streaming_config_sanitize(NULL);

    backend.active_radius_regions = -1;
    backend.preload_radius_regions = -1;
    backend.max_cached_regions = 0;
    rc_world_streaming_config_sanitize(&backend);
    assert(backend.active_radius_regions == 2);
    assert(backend.preload_radius_regions == 3);
    assert(backend.max_cached_regions == 64);

    RcWorldConfig preset = rc_preset_full_game();
    assert(preset.streaming.active_radius_regions == 2);
    assert(preset.streaming.preload_radius_regions == 3);
    assert(preset.streaming.max_cached_regions == 64);

    ViewerStreamingConfig viewer = viewer_streaming_config_default();
    assert(viewer.scene_radius_regions == 1);
    assert(viewer.preload_radius_regions == 0);
    assert(viewer.max_gpu_chunks == 128);
    assert(viewer.max_cpu_chunks == 128);
    assert(viewer.upload_budget_mb_per_frame == 16);
    size_t upload_budget = viewer_streaming_upload_budget_bytes(&viewer);
    assert(upload_budget == 16u * 1024u * 1024u);
    assert(viewer_streaming_upload_budget_admit(0, upload_budget,
                                                upload_budget));
    assert(!viewer_streaming_upload_budget_admit(1, upload_budget,
                                                 upload_budget));
    assert(viewer_streaming_upload_budget_admit(0, upload_budget + 1,
                                                upload_budget));
    assert(viewer_streaming_upload_budget_admit(upload_budget, 0,
                                                upload_budget));
    assert(!viewer_streaming_upload_budget_admit(0, 1, 0));
    assert(viewer_streaming_upload_budget_bytes(NULL) == 0);

    ViewerStreamingPlayerTransition transition = {0};
    viewer_streaming_player_transition_begin(
        &transition, 3081, 3421, 0, 1859, 5243, 0);
    assert(transition.active);
    assert(transition.source_x == 3081 && transition.source_y == 3421);
    assert(transition.source_plane == 0);
    int transition_x = -1;
    int transition_y = -1;
    int transition_plane = -1;
    assert(viewer_streaming_player_transition_source(
        &transition, &transition_x, &transition_y, &transition_plane));
    assert(transition_x == 3081 && transition_y == 3421);
    assert(transition_plane == 0 && transition.active);
    assert(viewer_streaming_player_transition_commit(
        &transition, &transition_x, &transition_y, &transition_plane));
    assert(transition_x == 1859 && transition_y == 5243);
    assert(transition_plane == 0 && !transition.active);
    assert(!viewer_streaming_player_transition_commit(
        &transition, &transition_x, &transition_y, &transition_plane));
    viewer_streaming_player_transition_begin(
        &transition, 3081, 3421, 0, 1859, 5243, 0);
    assert(!viewer_streaming_player_transition_commit(
        &transition, NULL, &transition_y, &transition_plane));
    assert(transition.active);
    assert(!viewer_streaming_player_transition_source(
        &transition, &transition_x, NULL, &transition_plane));
    assert(transition.active);
    viewer_streaming_player_transition_begin(
        NULL, 3081, 3421, 0, 1859, 5243, 0);
    viewer_streaming_config_sanitize(NULL);

    viewer.scene_radius_regions = -1;
    viewer.preload_radius_regions = -1;
    viewer.max_gpu_chunks = 0;
    viewer.max_cpu_chunks = 129;
    viewer.upload_budget_mb_per_frame = 0;
    viewer_streaming_config_sanitize(&viewer);
    assert(viewer.scene_radius_regions == 1);
    assert(viewer.preload_radius_regions == 0);
    assert(viewer.max_gpu_chunks == 128);
    assert(viewer.max_cpu_chunks == 128);
    assert(viewer.upload_budget_mb_per_frame == 16);

    ViewerStreamingTelemetry telemetry = {0};
    viewer_streaming_telemetry_record_load(&telemetry, 10.0, 4.0, 2.0);
    viewer_streaming_telemetry_record_load(&telemetry, 5.0, 1.5, 0.5);
    assert(telemetry.scene_load_count == 2);
    assert(telemetry.scene_or_chunk_load_ms == 5.0);
    assert(telemetry.scene_or_chunk_load_total_ms == 15.0);
    assert(telemetry.cpu_decode_ms == 1.5);
    assert(telemetry.cpu_decode_total_ms == 5.5);
    assert(telemetry.gpu_upload_ms == 0.5);
    assert(telemetry.gpu_upload_total_ms == 2.5);
    viewer_streaming_telemetry_record_load(&telemetry, -1.0, -2.0, -3.0);
    assert(telemetry.scene_load_count == 3);
    assert(telemetry.scene_or_chunk_load_ms == 0.0);
    assert(telemetry.scene_or_chunk_load_total_ms == 15.0);
    assert(telemetry.cpu_decode_ms == 0.0);
    assert(telemetry.cpu_decode_total_ms == 5.5);
    assert(telemetry.gpu_upload_ms == 0.0);
    assert(telemetry.gpu_upload_total_ms == 2.5);
    viewer_streaming_telemetry_record_load(NULL, 1.0, 1.0, 1.0);
    assert(viewer_streaming_now_ms() >= 0.0);

    ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
    viewer = viewer_streaming_config_default();
    int count = viewer_streaming_plan_mapsquares(
        &viewer, 3213, 3428, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    assert(count == 9);
    assert(plan[0].region_x == 50 && plan[0].region_y == 53);
    assert(viewer_streaming_mapsquare_in_plan(plan, count, 49, 52));
    assert(viewer_streaming_mapsquare_in_plan(plan, count, 51, 54));
    assert(!viewer_streaming_mapsquare_in_plan(plan, count, 52, 54));

    viewer.preload_radius_regions = 2;
    count = viewer_streaming_plan_mapsquares(
        &viewer, 3213, 3428, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    assert(count == 25);
    viewer.max_cpu_chunks = 24;
    assert(viewer_streaming_plan_mapsquares(
        &viewer, 3213, 3428, plan, VIEWER_STREAMING_CHUNK_CAPACITY) == -1);

    viewer = viewer_streaming_config_default();
    count = viewer_streaming_plan_mapsquares(
        &viewer, 0, 0, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    assert(count == 4);
    assert(plan[0].region_x == 0 && plan[0].region_y == 0);
    assert(viewer_streaming_plan_mapsquares(
        NULL, 0, 0, plan, VIEWER_STREAMING_CHUNK_CAPACITY) == -1);
    assert(viewer_streaming_plan_mapsquares(
        &viewer, -1, 0, plan, VIEWER_STREAMING_CHUNK_CAPACITY) == -1);
    assert(!viewer_streaming_mapsquare_in_plan(NULL, 1, 0, 0));
    assert(viewer_streaming_chunk_retained(plan, count, 0, 0, 0, 0, 2));
    assert(viewer_streaming_chunk_retained(plan, count, 0, 0, 2, 0, 2));
    assert(!viewer_streaming_chunk_retained(plan, count, 0, 0, 1, 0, 2));
    assert(!viewer_streaming_chunk_retained(plan, count, 2, 2, 0, 0, 2));
    assert(viewer_streaming_same_window(3136, 3392, 0, 3199, 3455, 0));
    assert(!viewer_streaming_same_window(3136, 3392, 0, 3200, 3455, 0));
    assert(!viewer_streaming_same_window(3136, 3392, 0, 3136, 3392, 1));
    assert(!viewer_streaming_same_window(-1, 3392, 0, 3136, 3392, 0));

    int prefetch_x = -1;
    int prefetch_y = -1;
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3160, 3424, 3264, 3424, 16,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 50 * 64 + 32 && prefetch_y == 53 * 64 + 32);
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3160, 3424, 3000, 3500, 16,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 48 * 64 + 32 && prefetch_y == 54 * 64 + 32);
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3188, 3424, 3189, 3424, 16,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 50 * 64 + 32 && prefetch_y == 53 * 64 + 32);
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3137, 3424, 3136, 3424, 16,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 48 * 64 + 32 && prefetch_y == 53 * 64 + 32);
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3160, 3448, 3160, 3449, 16,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 49 * 64 + 32 && prefetch_y == 54 * 64 + 32);
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3160, 3393, 3160, 3392, 16,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 49 * 64 + 32 && prefetch_y == 52 * 64 + 32);
    assert(viewer_streaming_predict_prefetch_center(
        49, 53, 3168, 3424, 3169, 3424, 99,
        &prefetch_x, &prefetch_y));
    assert(prefetch_x == 50 * 64 + 32 && prefetch_y == 53 * 64 + 32);
    assert(!viewer_streaming_predict_prefetch_center(
        0, 0, 1, 1, 0, 1, 16, &prefetch_x, &prefetch_y));
    assert(!viewer_streaming_predict_prefetch_center(
        49, 53, 3160, 3424, 3161, 3424, 16,
        &prefetch_x, &prefetch_y));
    assert(!viewer_streaming_predict_prefetch_center(
        49, 53, 3160, 3424, 3264, 3424, 16, NULL, &prefetch_y));

    char catalog_path[128];
    snprintf(catalog_path, sizeof(catalog_path),
             "/tmp/runec_mapsquare_catalog_%ld.bin", (long)getpid());
    FILE *catalog_file = fopen(catalog_path, "wb");
    assert(catalog_file);
    uint32_t catalog_header[2] = {
        VIEWER_STREAMING_MAPSQUARE_CATALOG_MAGIC, 6,
    };
    const uint8_t catalog_entries[][2] = {
        {28, 80}, {29, 80}, {30, 80},
        {28, 81}, {29, 81}, {28, 82},
    };
    assert(fwrite(catalog_header, sizeof(catalog_header), 1, catalog_file)
           == 1);
    assert(fwrite(catalog_entries, sizeof(catalog_entries), 1, catalog_file)
           == 1);
    assert(fclose(catalog_file) == 0);

    ViewerMapsquareCatalog catalog = {0};
    assert(viewer_streaming_catalog_load(&catalog, catalog_path));
    assert(catalog.loaded && catalog.count == 6);
    assert(viewer_streaming_catalog_contains(&catalog, 29, 81));
    assert(!viewer_streaming_catalog_contains(&catalog, 30, 81));
    assert(!viewer_streaming_catalog_contains(&catalog, -1, 81));
    count = viewer_streaming_plan_mapsquares(
        &viewer, 1859, 5243, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    assert(count == 9);
    count = viewer_streaming_filter_mapsquares(&catalog, plan, count);
    assert(count == 6);
    assert(viewer_streaming_mapsquare_in_plan(plan, count, 29, 81));
    assert(!viewer_streaming_mapsquare_in_plan(plan, count, 30, 81));
    assert(viewer_streaming_filter_mapsquares(NULL, plan, count) == count);
    assert(viewer_streaming_filter_mapsquares(&catalog, NULL, count) == -1);
    assert(remove(catalog_path) == 0);
    assert(!viewer_streaming_catalog_load(&catalog, catalog_path));
    assert(!viewer_streaming_catalog_load(NULL, catalog_path));

    catalog_file = fopen(catalog_path, "wb");
    assert(catalog_file);
    catalog_header[1] = 2;
    const uint8_t unsorted_entries[][2] = {{29, 81}, {28, 81}};
    assert(fwrite(catalog_header, sizeof(catalog_header), 1, catalog_file)
           == 1);
    assert(fwrite(unsorted_entries, sizeof(unsorted_entries), 1, catalog_file)
           == 1);
    assert(fclose(catalog_file) == 0);
    assert(!viewer_streaming_catalog_load(&catalog, catalog_path));
    assert(!catalog.loaded);
    assert(!viewer_streaming_catalog_contains(&catalog, 29, 81));
    assert(remove(catalog_path) == 0);

    catalog_file = fopen(catalog_path, "wb");
    assert(catalog_file);
    catalog_header[1] = 1;
    assert(fwrite(catalog_header, sizeof(catalog_header), 1, catalog_file)
           == 1);
    assert(fwrite(catalog_entries, sizeof(catalog_entries[0]), 1,
                  catalog_file) == 1);
    assert(fputc(0, catalog_file) == 0);
    assert(fclose(catalog_file) == 0);
    assert(!viewer_streaming_catalog_load(&catalog, catalog_path));
    assert(remove(catalog_path) == 0);

    char path[128];
    assert(viewer_streaming_mapsquare_path(
        path, sizeof(path), "data/regions", 50, 53, 2, ".terrain"));
    assert(strcmp(path, "data/regions/50_53.p2.terrain") == 0);
    assert(!viewer_streaming_mapsquare_path(
        path, 4, "data/regions", 50, 53, 2, ".terrain"));
    assert(!viewer_streaming_mapsquare_path(
        path, sizeof(path), "data/regions", 50, 53, 4, ".terrain"));

    printf("test_streaming_config: defaults and counters verified.\n");
    return 0;
}
