#include "xpc_internal.h"
#include <stdarg.h>

static xpc_object_t get(xpc_object_t o, const char *key) {
    return xpc_dictionary_get_value(o, key);
}

xpc_object_t xpc_uuid_create(const uuid_t uuid) {
    if (!uuid) return NULL;
    xpc_uuid_t *u = XPC_CAST(xpc_uuid_t,
        xpc_object_alloc(&_xpc_type_uuid, sizeof(*u)));
    if (!u) return NULL;
    uuid_copy(u->uuid, uuid);
    return (xpc_object_t)u;
}

const uint8_t *xpc_uuid_get_bytes(xpc_object_t o) {
    return XPC_OBJECT_CHECK(o, &_xpc_type_uuid) ? XPC_CAST(xpc_uuid_t, o)->uuid : NULL;
}

xpc_object_t xpc_date_create_from_timespec(struct timespec *ts) {
    return ts ? xpc_date_create((int64_t)ts->tv_sec * 1000000000ll + ts->tv_nsec) : NULL;
}

void xpc_date_get_timespec(xpc_object_t o, struct timespec *ts) {
    if (!ts) return;
    int64_t n = xpc_date_get_value(o);
    ts->tv_sec = n / 1000000000ll;
    ts->tv_nsec = n % 1000000000ll;
}

bool xpc_data_get_bytes(xpc_object_t o, void *bytes, size_t off, size_t len) {
    if (!XPC_OBJECT_CHECK(o, &_xpc_type_data) || !bytes) return false;
    xpc_data_t *d = XPC_CAST(xpc_data_t, o);
    if (off > d->length || len > d->length - off) return false;
    memcpy(bytes, d->data + off, len);
    return true;
}

xpc_object_t xpc_string_create_with_format(const char *fmt, ...) {
    if (!fmt) return NULL;
    va_list ap, copy;
    va_start(ap, fmt); va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy); va_end(copy);
    if (n < 0) { va_end(ap); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { va_end(ap); return NULL; }
    vsnprintf(buf, (size_t)n + 1, fmt, ap); va_end(ap);
    xpc_object_t o = xpc_string_create_with_length(buf, (size_t)n);
    free(buf); return o;
}

static xpc_object_t array_value(xpc_object_t o, size_t i) {
    return xpc_array_get_value(o, i);
}
bool xpc_array_get_bool(xpc_object_t o, size_t i) { return xpc_bool_get_value(array_value(o,i)); }
int64_t xpc_array_get_int64(xpc_object_t o, size_t i) { return xpc_int64_get_value(array_value(o,i)); }
uint64_t xpc_array_get_uint64(xpc_object_t o, size_t i) { return xpc_uint64_get_value(array_value(o,i)); }
double xpc_array_get_double(xpc_object_t o, size_t i) { return xpc_double_get_value(array_value(o,i)); }
const char *xpc_array_get_string(xpc_object_t o, size_t i) { return xpc_string_get_string_ptr(array_value(o,i)); }
const void *xpc_array_get_data(xpc_object_t o, size_t i, size_t *n) {
    xpc_object_t v = array_value(o,i); if (n) *n = xpc_data_get_length(v); return xpc_data_get_bytes_ptr(v);
}
bool xpc_array_get_data_np(xpc_object_t o, size_t i, const void **p, size_t *n) {
    xpc_object_t v=array_value(o,i); if (!XPC_OBJECT_CHECK(v,&_xpc_type_data)) return false;
    if (p) *p=xpc_data_get_bytes_ptr(v); if (n) *n=xpc_data_get_length(v); return true;
}
const uint8_t *xpc_array_get_uuid(xpc_object_t o, size_t i) { return xpc_uuid_get_bytes(array_value(o,i)); }
xpc_object_t xpc_array_get_date(xpc_object_t o, size_t i) { xpc_object_t v=array_value(o,i); return XPC_OBJECT_CHECK(v,&_xpc_type_date)?v:NULL; }

#define DICT_GETTER(name, type, fn, fallback) \
type name(xpc_object_t o,const char *k){ xpc_object_t v=get(o,k); return fn(v); }
DICT_GETTER(xpc_dictionary_get_bool,bool,xpc_bool_get_value,false)
DICT_GETTER(xpc_dictionary_get_int64,int64_t,xpc_int64_get_value,0)
DICT_GETTER(xpc_dictionary_get_uint64,uint64_t,xpc_uint64_get_value,0)
DICT_GETTER(xpc_dictionary_get_double,double,xpc_double_get_value,0.0)
const char *xpc_dictionary_get_string(xpc_object_t o,const char *k){return xpc_string_get_string_ptr(get(o,k));}
const void *xpc_dictionary_get_data(xpc_object_t o,const char *k,size_t *n){xpc_object_t v=get(o,k);if(n)*n=xpc_data_get_length(v);return xpc_data_get_bytes_ptr(v);}
bool xpc_dictionary_get_data_np(xpc_object_t o,const char *k,const void **p,size_t *n){xpc_object_t v=get(o,k);if(!XPC_OBJECT_CHECK(v,&_xpc_type_data))return false;if(p)*p=xpc_data_get_bytes_ptr(v);if(n)*n=xpc_data_get_length(v);return true;}
const uint8_t *xpc_dictionary_get_uuid(xpc_object_t o,const char *k){return xpc_uuid_get_bytes(get(o,k));}
xpc_object_t xpc_dictionary_get_date(xpc_object_t o,const char *k){xpc_object_t v=get(o,k);return XPC_OBJECT_CHECK(v,&_xpc_type_date)?v:NULL;}
void xpc_dictionary_remove_value(xpc_object_t o,const char *k){xpc_dictionary_set_value(o,k,NULL);}
#define DICT_SETTER(name, ctor, type) void name(xpc_object_t o,const char*k,type v){xpc_object_t n=ctor(v);xpc_dictionary_set_value(o,k,n);xpc_release(n);}
DICT_SETTER(xpc_dictionary_set_bool,xpc_bool_create,bool)
DICT_SETTER(xpc_dictionary_set_int64,xpc_int64_create,int64_t)
DICT_SETTER(xpc_dictionary_set_uint64,xpc_uint64_create,uint64_t)
DICT_SETTER(xpc_dictionary_set_double,xpc_double_create,double)
void xpc_dictionary_set_string(xpc_object_t o,const char*k,const char*v){xpc_object_t n=xpc_string_create(v);xpc_dictionary_set_value(o,k,n);xpc_release(n);}
void xpc_dictionary_set_data(xpc_object_t o,const char*k,const void*p,size_t n){xpc_object_t v=xpc_data_create(p,n);xpc_dictionary_set_value(o,k,v);xpc_release(v);}
void xpc_dictionary_set_uuid(xpc_object_t o,const char*k,const uuid_t u){xpc_object_t v=xpc_uuid_create(u);xpc_dictionary_set_value(o,k,v);xpc_release(v);}
void xpc_dictionary_set_date(xpc_object_t o,const char*k,int64_t v){xpc_object_t n=xpc_date_create(v);xpc_dictionary_set_value(o,k,n);xpc_release(n);}

char *xpc_copy_description(xpc_object_t o) { return xpc_description_create(o); }
