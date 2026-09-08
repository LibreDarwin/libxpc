/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
