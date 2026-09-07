#include "xpc_internal.h"

struct _xpc_pipe_s { mach_port_t port; uint64_t flags; bool valid; };

xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags) {
    xpc_pipe_t p = calloc(1, sizeof(*p));
    if (p) { p->port = port; p->flags = flags; p->valid = true; }
    return p;
}

int xpc_pipe_invalidate(xpc_pipe_t p) {
    if (!p) return KERN_INVALID_ARGUMENT;
    p->valid = false; free(p); return KERN_SUCCESS;
}

static int send(xpc_pipe_t p, xpc_object_t object, xpc_object_t *reply,
    uint32_t message_id) {
    (void)message_id;
    if (reply) *reply = NULL;
    if (!p || !p->valid || !object) return KERN_INVALID_ARGUMENT;
    /* The core milestone exposes serialization and keeps Mach transport
     * deliberately narrow until endpoint/connection objects land. */
    return KERN_NOT_SUPPORTED;
}

int xpc_pipe_simpleroutine(xpc_pipe_t p, xpc_object_t o, xpc_object_t *r) {
    return send(p, o, r, XPC_PIPE_ID_SIMPLEROUTINE);
}
int xpc_pipe_routine(xpc_pipe_t p, xpc_object_t o, xpc_object_t *r) {
    return send(p, o, r, XPC_PIPE_ID_ROUTINE);
}
