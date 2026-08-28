#ifndef RUNEC_VIEWER_ACTOR_PROJECTION_H
#define RUNEC_VIEWER_ACTOR_PROJECTION_H

float runec_actor_model_midpoint(float min_y, float max_y, float scale);
int runec_actor_model_vertical_bounds(const float *vertices, int vertex_count,
                                      float *min_y, float *max_y);
float runec_npc_target_angle(int npc_x, int npc_y, int npc_size,
                             int target_x, int target_y, float fallback);

#endif
