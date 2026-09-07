#include "xpc_internal.h"

static void append(char **p, size_t *cap, size_t *len, const char *s) {
    size_t n=strlen(s); if (*len+n+1>*cap) { while(*len+n+1>*cap)*cap*=2; *p=realloc(*p,*cap); }
    memcpy(*p+*len,s,n); *len+=n; (*p)[*len]=0;
}
static void desc(xpc_object_t o,char **p,size_t*c,size_t*l) {
    char b[96]; if(!o){append(p,c,l,"<null>");return;}
    switch(o->isa->kind){
    case XPC_KIND_NULL: append(p,c,l,"<null>");break;
    case XPC_KIND_BOOL: append(p,c,l,xpc_bool_get_value(o)?"true":"false");break;
    case XPC_KIND_INT64: snprintf(b,sizeof b,"%lld",(long long)xpc_int64_get_value(o));append(p,c,l,b);break;
    case XPC_KIND_UINT64: snprintf(b,sizeof b,"%llu",(unsigned long long)xpc_uint64_get_value(o));append(p,c,l,b);break;
    case XPC_KIND_DOUBLE: snprintf(b,sizeof b,"%g",xpc_double_get_value(o));append(p,c,l,b);break;
    case XPC_KIND_STRING: append(p,c,l,"\"");append(p,c,l,xpc_string_get_string_ptr(o));append(p,c,l,"\"");break;
    case XPC_KIND_DATA: snprintf(b,sizeof b,"<data %zu bytes>",xpc_data_get_length(o));append(p,c,l,b);break;
    case XPC_KIND_ARRAY: append(p,c,l,"[");for(size_t i=0;i<xpc_array_get_count(o);i++){if(i)append(p,c,l,", ");desc(xpc_array_get_value(o,i),p,c,l);}append(p,c,l,"]");break;
    case XPC_KIND_DICTIONARY: append(p,c,l,"{");{__block bool first=true;xpc_dictionary_apply(o,^bool(const char*k,xpc_object_t v){if(!first)append(p,c,l,", ");first=false;append(p,c,l,k);append(p,c,l," = ");desc(v,p,c,l);return true;});}append(p,c,l,"}");break;
    default: append(p,c,l,o->isa->name);break;
    }
}
char *xpc_description_create(xpc_object_t o){size_t c=128,l=0;char*p=calloc(1,c);if(p)desc(o,&p,&c,&l);return p;}
