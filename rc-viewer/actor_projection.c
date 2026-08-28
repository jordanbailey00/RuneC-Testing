#include "actor_projection.h"

#include <math.h>

float runec_actor_model_midpoint(float min_y, float max_y, float scale) {
    if (max_y < min_y) {
        float swap = min_y;
        min_y = max_y;
        max_y = swap;
    }
    return (min_y + max_y) * 0.5f * scale;
}

int runec_actor_model_vertical_bounds(const float *vertices, int vertex_count,
                                      float *min_y, float *max_y) {
    if (!vertices || vertex_count <= 0 || !min_y || !max_y) return 0;
    float low = vertices[1];
    float high = vertices[1];
    if (!isfinite(low)) return 0;
    for (int i = 1; i < vertex_count; i++) {
        float y = vertices[i * 3 + 1];
        if (!isfinite(y)) return 0;
        if (y < low) low = y;
        if (y > high) high = y;
    }
    *min_y = low;
    *max_y = high;
    return 1;
}

float runec_npc_target_angle(int npc_x, int npc_y, int npc_size,
                             int target_x, int target_y, float fallback) {
    if (npc_size < 1) npc_size = 1;
    float from_x = (float)npc_x + (float)npc_size * 0.5f;
    float from_y = (float)npc_y + (float)npc_size * 0.5f;
    float to_x = (float)target_x + 0.5f;
    float to_y = (float)target_y + 0.5f;
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    if (fabsf(dx) < 0.0001f && fabsf(dy) < 0.0001f) return fallback;
    return atan2f(dx, -dy) * (180.0f / 3.14159265358979323846f);
}
