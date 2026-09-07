#include "xpc_internal.h"
#include <assert.h>

int main(void) {
    xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(d, "answer", 42);
    xpc_dictionary_set_string(d, "text", "hello");
    const uint8_t bytes[] = { 1, 2, 3, 4, 5 };
    xpc_dictionary_set_data(d, "blob", bytes, sizeof(bytes));
    xpc_object_t a = xpc_array_create(NULL, 0);
    xpc_array_append_value(a, xpc_bool_create(true));
    xpc_object_t nested = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(nested, "n", 7);
    xpc_array_append_value(a, nested);
    xpc_release(nested);
    xpc_dictionary_set_value(d, "array", a);
    xpc_release(a);

    size_t len = 0;
    uint8_t *wire = xpc_wire_serialize(d, XPC_PIPE_ID_SIMPLEROUTINE, &len);
    assert(wire && len > 40);
    xpc_object_t copy = xpc_wire_deserialize(wire, len);
    assert(copy && xpc_equal(d, copy));
    assert(xpc_dictionary_get_int64(copy, "answer") == 42);
    assert(strcmp(xpc_dictionary_get_string(copy, "text"), "hello") == 0);
    free(wire);
    xpc_release(copy); xpc_release(d);
    return 0;
}
