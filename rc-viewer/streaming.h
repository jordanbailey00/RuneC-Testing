#ifndef RUNEC_VIEWER_STREAMING_H
#define RUNEC_VIEWER_STREAMING_H

#include <stdint.h>

enum {
    VIEWER_STREAMING_CHUNK_CAPACITY = 128,
    VIEWER_STREAMING_DEFAULT_SCENE_RADIUS = 1,
    VIEWER_STREAMING_DEFAULT_PRELOAD_RADIUS = 0,
    VIEWER_STREAMING_DEFAULT_MAX_GPU_CHUNKS = 128,
    VIEWER_STREAMING_DEFAULT_MAX_CPU_CHUNKS = 128,
    VIEWER_STREAMING_DEFAULT_UPLOAD_BUDGET_MB = 16,
};

typedef struct {
    int scene_radius_regions;
    int preload_radius_regions;
    int max_gpu_chunks;
    int max_cpu_chunks;
    int upload_budget_mb_per_frame;
} ViewerStreamingConfig;

typedef struct {
    uint64_t scene_load_count;
    double startup_ms;
    double scene_or_chunk_load_ms;
    double scene_or_chunk_load_total_ms;
    double cpu_decode_ms;
    double cpu_decode_total_ms;
    double gpu_upload_ms;
    double gpu_upload_total_ms;
    int terrain_chunks_cpu;
    int terrain_chunks_gpu;
    int object_chunks_cpu;
    int object_chunks_gpu;
    uint64_t terrain_vertices_resident;
    uint64_t object_vertices_resident;
    double texture_cache_mb;
    double model_cache_mb;
    int active_npcs;
    int active_ground_items;
    int backend_pages_loaded;
    double backend_page_load_ms;
    double backend_active_area_load_ms;
    int draw_calls_estimate;
} ViewerStreamingTelemetry;

ViewerStreamingConfig viewer_streaming_config_default(void);
void viewer_streaming_config_sanitize(ViewerStreamingConfig *config);
double viewer_streaming_now_ms(void);
void viewer_streaming_telemetry_record_load(ViewerStreamingTelemetry *telemetry,
                                            double load_ms,
                                            double cpu_decode_ms,
                                            double gpu_upload_ms);

#endif
