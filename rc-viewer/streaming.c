#include "streaming.h"

#include <time.h>

ViewerStreamingConfig viewer_streaming_config_default(void) {
    return (ViewerStreamingConfig){
        .scene_radius_regions = VIEWER_STREAMING_DEFAULT_SCENE_RADIUS,
        .preload_radius_regions = VIEWER_STREAMING_DEFAULT_PRELOAD_RADIUS,
        .max_gpu_chunks = VIEWER_STREAMING_DEFAULT_MAX_GPU_CHUNKS,
        .max_cpu_chunks = VIEWER_STREAMING_DEFAULT_MAX_CPU_CHUNKS,
        .upload_budget_mb_per_frame =
            VIEWER_STREAMING_DEFAULT_UPLOAD_BUDGET_MB,
    };
}

void viewer_streaming_config_sanitize(ViewerStreamingConfig *config) {
    if (!config) return;
    ViewerStreamingConfig defaults = viewer_streaming_config_default();
    if (config->scene_radius_regions < 0)
        config->scene_radius_regions = defaults.scene_radius_regions;
    if (config->preload_radius_regions < 0)
        config->preload_radius_regions = defaults.preload_radius_regions;
    if (config->max_gpu_chunks <= 0
            || config->max_gpu_chunks > VIEWER_STREAMING_CHUNK_CAPACITY) {
        config->max_gpu_chunks = defaults.max_gpu_chunks;
    }
    if (config->max_cpu_chunks <= 0
            || config->max_cpu_chunks > VIEWER_STREAMING_CHUNK_CAPACITY) {
        config->max_cpu_chunks = defaults.max_cpu_chunks;
    }
    if (config->upload_budget_mb_per_frame <= 0) {
        config->upload_budget_mb_per_frame =
            defaults.upload_budget_mb_per_frame;
    }
}

double viewer_streaming_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

void viewer_streaming_telemetry_record_load(ViewerStreamingTelemetry *telemetry,
                                            double load_ms,
                                            double cpu_decode_ms,
                                            double gpu_upload_ms) {
    if (!telemetry) return;
    if (load_ms < 0.0) load_ms = 0.0;
    if (cpu_decode_ms < 0.0) cpu_decode_ms = 0.0;
    if (gpu_upload_ms < 0.0) gpu_upload_ms = 0.0;
    telemetry->scene_load_count++;
    telemetry->scene_or_chunk_load_ms = load_ms;
    telemetry->scene_or_chunk_load_total_ms += load_ms;
    telemetry->cpu_decode_ms = cpu_decode_ms;
    telemetry->cpu_decode_total_ms += cpu_decode_ms;
    telemetry->gpu_upload_ms = gpu_upload_ms;
    telemetry->gpu_upload_total_ms += gpu_upload_ms;
}
