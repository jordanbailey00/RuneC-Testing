#include <assert.h>
#include <stdio.h>

#include "assets.h"
#include "models.h"
#include "objects.h"
#include "terrain.h"

typedef struct {
    int terrain_vertices;
    int object_vertices;
    int object_anims;
    int model_count;
    size_t terrain_upload_bytes;
    size_t object_upload_bytes;
    size_t model_upload_bytes;
} CpuAssetSnapshot;

static CpuAssetSnapshot load_snapshot(RcAssetBackend backend) {
    rc_asset_reset();
    rc_asset_set_backend(backend);

    TerrainMesh *terrain = terrain_load_cpu(
        "data/regions/49_53.p0.terrain");
    ObjectMesh *objects = objects_load_cpu_with_shared_atlas(
        "data/regions/49_53.p0.objects");
    assert(terrain != NULL && !terrain->loaded);
    assert(objects != NULL && !objects->loaded);

    ModelSet *models = models_load_cpu_with_shared_atlas(
        "data/regions/49_53.p0.object_anim.models");
    assert(models != NULL && models->loaded);

    TerrainMesh *empty = terrain_load_cpu(
        "data/regions/28_80.p0.terrain");
    ObjectMesh *empty_objects = objects_load_cpu_with_shared_atlas(
        "data/regions/28_80.p0.objects");
    assert(empty != NULL && !empty->loaded && empty->vertex_count == 0);
    assert(empty_objects != NULL && !empty_objects->loaded
           && empty_objects->total_vertex_count == 0
           && empty_objects->object_anim_count > 0);
    assert(empty->heightmap != NULL);
    int empty_origin_x = empty->hm_min_x;
    int empty_origin_y = empty->hm_min_y;
    terrain_offset(empty, empty_origin_x, empty_origin_y);
    assert(empty->hm_min_x == 0 && empty->hm_min_y == 0);
    assert(empty->min_world_x == 0 && empty->min_world_y == 0);

    CpuAssetSnapshot snapshot = {
        .terrain_vertices = terrain->vertex_count,
        .object_vertices = objects->total_vertex_count,
        .object_anims = objects->object_anim_count,
        .model_count = models->count,
        .terrain_upload_bytes = terrain_upload_bytes(terrain),
        .object_upload_bytes = objects_upload_bytes(objects),
        .model_upload_bytes = models_upload_bytes(models),
    };
    assert(snapshot.terrain_vertices > 0);
    assert(snapshot.object_vertices > 0);
    assert(snapshot.object_anims > 0);
    assert(snapshot.terrain_upload_bytes > 0);
    assert(snapshot.object_upload_bytes > 0);
    assert(snapshot.model_upload_bytes > 0);

    objects_free(empty_objects);
    terrain_free(empty);
    models_free(models);
    objects_free(objects);
    terrain_free(terrain);
    return snapshot;
}

int main(void) {
    CpuAssetSnapshot loose = load_snapshot(RC_ASSET_BACKEND_LOOSE);
    CpuAssetSnapshot packed = load_snapshot(RC_ASSET_BACKEND_PACK);
    assert(loose.terrain_vertices == packed.terrain_vertices);
    assert(loose.object_vertices == packed.object_vertices);
    assert(loose.object_anims == packed.object_anims);
    assert(loose.model_count == packed.model_count);
    assert(loose.terrain_upload_bytes == packed.terrain_upload_bytes);
    assert(loose.object_upload_bytes == packed.object_upload_bytes);
    assert(loose.model_upload_bytes == packed.model_upload_bytes);
    rc_asset_reset();
    printf("test_streaming_cpu_assets: loose/pack CPU assets match.\n");
    return 0;
}
