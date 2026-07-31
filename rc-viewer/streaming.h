#ifndef RUNEC_VIEWER_STREAMING_H
#define RUNEC_VIEWER_STREAMING_H

#include <stddef.h>
#include <stdint.h>

enum {
    VIEWER_STREAMING_CHUNK_CAPACITY = 128,
    VIEWER_STREAMING_DEFAULT_SCENE_RADIUS = 1,
    VIEWER_STREAMING_DEFAULT_PRELOAD_RADIUS = 0,
    VIEWER_STREAMING_DEFAULT_MAX_GPU_CHUNKS = 128,
    VIEWER_STREAMING_DEFAULT_MAX_CPU_CHUNKS = 128,
    VIEWER_STREAMING_DEFAULT_UPLOAD_BUDGET_MB = 16,
    VIEWER_STREAMING_MAPSQUARE_SIZE = 64,
    VIEWER_STREAMING_MAX_MAPSQUARE_COORD = 255,
    VIEWER_STREAMING_MAPSQUARE_CATALOG_BYTES = 8192,
};

#define VIEWER_STREAMING_MAPSQUARE_CATALOG_MAGIC 0x3151534du

typedef struct {
    int region_x;
    int region_y;
} ViewerMapsquareCoord;

typedef struct {
    uint8_t present[VIEWER_STREAMING_MAPSQUARE_CATALOG_BYTES];
    uint32_t count;
    int loaded;
} ViewerMapsquareCatalog;

typedef struct {
    int scene_radius_regions;
    int preload_radius_regions;
    int max_gpu_chunks;
    int max_cpu_chunks;
    int upload_budget_mb_per_frame;
} ViewerStreamingConfig;

typedef struct {
    int active;
    int source_x, source_y, source_plane;
    int destination_x, destination_y, destination_plane;
} ViewerStreamingPlayerTransition;

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
    int async_jobs_pending;
    int cpu_chunks_staged;
    uint64_t gpu_upload_bytes_frame;
    uint64_t gpu_upload_bytes_peak;
    uint64_t gpu_upload_budget_bytes;
    uint64_t async_frames;
    double main_thread_streaming_work_max_ms;
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
int viewer_streaming_plan_mapsquares(
    const ViewerStreamingConfig *config,
    int tile_x,
    int tile_y,
    ViewerMapsquareCoord *out,
    int capacity);
int viewer_streaming_mapsquare_in_plan(const ViewerMapsquareCoord *plan,
                                       int count, int region_x, int region_y);
int viewer_streaming_catalog_load(ViewerMapsquareCatalog *catalog,
                                  const char *path);
int viewer_streaming_catalog_contains(const ViewerMapsquareCatalog *catalog,
                                      int region_x, int region_y);
int viewer_streaming_filter_mapsquares(
    const ViewerMapsquareCatalog *catalog,
    ViewerMapsquareCoord *plan,
    int count);
int viewer_streaming_mapsquare_path(char *out, size_t capacity,
                                    const char *directory, int region_x,
                                    int region_y, int plane,
                                    const char *suffix);
size_t viewer_streaming_upload_budget_bytes(
    const ViewerStreamingConfig *config);
int viewer_streaming_upload_budget_admit(size_t used_bytes,
                                         size_t upload_bytes,
                                         size_t budget_bytes);
void viewer_streaming_player_transition_begin(
    ViewerStreamingPlayerTransition *transition,
    int source_x, int source_y, int source_plane,
    int destination_x, int destination_y, int destination_plane);
int viewer_streaming_player_transition_source(
    const ViewerStreamingPlayerTransition *transition,
    int *x, int *y, int *plane);
int viewer_streaming_player_transition_commit(
    ViewerStreamingPlayerTransition *transition,
    int *x, int *y, int *plane);

#endif
