#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "streaming_async.h"

typedef struct {
    int value;
    useconds_t delay_us;
} TestRequest;

typedef struct {
    atomic_int request_frees;
    atomic_int payload_frees;
} TestState;

static void *decode_request(void *opaque, size_t *upload_bytes,
                            double *decode_ms, void *user) {
    (void)user;
    TestRequest *request = opaque;
    if (request->delay_us > 0)
        usleep(request->delay_us);
    if (request->value < 0)
        return NULL;
    int *payload = malloc(sizeof(*payload));
    assert(payload != NULL);
    *payload = request->value * 2;
    *upload_bytes = (size_t)request->value;
    *decode_ms = 1.25;
    return payload;
}

static void destroy_request(void *opaque, void *user) {
    TestState *state = user;
    atomic_fetch_add(&state->request_frees, 1);
    free(opaque);
}

static void destroy_payload(void *opaque, void *user) {
    TestState *state = user;
    atomic_fetch_add(&state->payload_frees, 1);
    free(opaque);
}

static TestRequest *request_new(int value, useconds_t delay_us) {
    TestRequest *request = malloc(sizeof(*request));
    assert(request != NULL);
    *request = (TestRequest){value, delay_us};
    return request;
}

static int wait_result(ViewerAsyncLoader *loader, ViewerAsyncResult *out) {
    for (int i = 0; i < 1000; i++) {
        if (viewer_async_loader_poll(loader, out))
            return 1;
        usleep(1000);
    }
    return 0;
}

int main(void) {
    TestState state = {0};
    ViewerAsyncLoader loader;
    assert(viewer_async_loader_init(&loader, decode_request,
                                    destroy_request, destroy_payload,
                                    &state));

    assert(viewer_async_loader_submit(&loader, 1, 10,
                                      request_new(64, 0)));
    TestRequest *duplicate = request_new(65, 0);
    assert(!viewer_async_loader_submit(&loader, 1, 10, duplicate));
    destroy_request(duplicate, &state);
    ViewerAsyncResult result = {0};
    assert(wait_result(&loader, &result));
    assert(result.generation == 1 && result.key == 10 && result.success);
    assert(*(int *)result.payload == 128);
    assert(result.upload_bytes == 64 && result.decode_ms == 1.25);
    viewer_async_loader_release_result(&loader, &result);

    assert(viewer_async_loader_submit(&loader, 1, 11,
                                      request_new(-1, 0)));
    assert(wait_result(&loader, &result));
    assert(!result.success && result.payload == NULL);
    viewer_async_loader_release_result(&loader, &result);

    assert(viewer_async_loader_submit(&loader, 1, 12,
                                      request_new(12, 30000)));
    assert(viewer_async_loader_submit(&loader, 1, 13,
                                      request_new(13, 0)));
    viewer_async_loader_set_generation(&loader, 2);
    assert(viewer_async_loader_submit(&loader, 2, 20,
                                      request_new(20, 0)));
    assert(wait_result(&loader, &result));
    assert(result.generation == 2 && result.key == 20);
    viewer_async_loader_release_result(&loader, &result);
    assert(viewer_async_loader_pending(&loader, 1) == 0);
    assert(viewer_async_loader_pending(&loader, 2) == 0);

    viewer_async_loader_destroy(&loader);
    assert(atomic_load(&state.request_frees) == 6);
    assert(atomic_load(&state.payload_frees) >= 2);
    return 0;
}
