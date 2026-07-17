#include <assert.h>
#include <stdio.h>

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
    assert(backend.max_cached_regions == 10);
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

    printf("test_streaming_config: defaults and counters verified.\n");
    return 0;
}
