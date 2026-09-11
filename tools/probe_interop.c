/*
 * tools/probe_interop.c — find the exact message framing real launchd
 * accepts for a routine request, using OUR serializer over OUR wire
 * encoding (proven byte-exact by probe4b) against the REAL domain port.
 *
 * Variants tested (8s receive timeout, so a drop ≠ infinite hang):
 *   0: simple message, no descriptor          (current xpc_pipe.c behavior)
 *   1: complex via mach_msg_base_t, descriptor at offset 32
 *   2: complex, packed descriptor at offset 28
 *   3: complex via mach_msg_base_t + voucher=COPY_SEND of minted voucher
 *   4: simple + voucher_mach_msg_set() — the exact real-libxpc mechanism
 *   5: simple + voucher_mach_msg_set() + real key order (handle first)
 *
 * Exit 0 if any variant got an answer, 1 otherwise.
 */

#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_voucher.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpc.h"

typedef uint8_t *(*wire_serialize_fn)(xpc_object_t, uint32_t, size_t *);
typedef xpc_object_t (*wire_deserialize_fn)(void *, size_t);

static wire_serialize_fn   xpc_wire_serialize_sym;
static wire_deserialize_fn xpc_wire_deserialize_sym;

static const char *
kerr(kern_return_t kr)
{
    switch (kr) {
    case KERN_SUCCESS: return "KERN_SUCCESS";
    case MACH_SEND_INVALID_DEST: return "SEND_INVALID_DEST";
    case MACH_SEND_INVALID_REPLY: return "SEND_INVALID_REPLY";
    case MACH_SEND_INVALID_VOUCHER: return "SEND_INVALID_VOUCHER";
    case MACH_SEND_INVALID_HEADER: return "SEND_INVALID_HEADER";
    case MACH_RCV_TIMED_OUT: return "RCV_TIMED_OUT";
    case MACH_RCV_INVALID_NAME: return "RCV_INVALID_NAME";
    default: return "?";
    }
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* serial runs hang otherwise */
    mach_port_t bp = MACH_PORT_NULL;
    kern_return_t kr = task_get_bootstrap_port(mach_task_self(), &bp);
    printf("[*] bootstrap port = %#x (kr %#x: %s)\n", bp, kr, kerr(kr));
    if (kr != KERN_SUCCESS || !MACH_PORT_VALID(bp)) return 1;

    xpc_wire_serialize_sym = (wire_serialize_fn)dlsym(RTLD_DEFAULT, "xpc_wire_serialize");
    xpc_wire_deserialize_sym = (wire_deserialize_fn)dlsym(RTLD_DEFAULT, "xpc_wire_deserialize");
    if (!xpc_wire_serialize_sym || !xpc_wire_deserialize_sym) {
        printf("[!] wire functions not found in this process\n");
        return 1;
    }

    xpc_object_t req = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(req, "type", 1);
    xpc_dictionary_set_uint64(req, "handle", 0);

    size_t ser_len = 0;
    uint8_t *ser = xpc_wire_serialize_sym(req, 0x4000032f, &ser_len);
    if (!ser || ser_len < 24) { printf("[!] serialize failed\n"); return 1; }

    /* The exact dict our xpc_domain_routine sends on every routine:
     * {type:7, handle:0, pre-exec:true, subsystem:3, routine:0x32f}. */
    xpc_object_t lc = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(lc, "type", 7);
    xpc_dictionary_set_uint64(lc, "handle", 0);
    xpc_dictionary_set_bool(lc, "pre-exec", true);
    xpc_dictionary_set_uint64(lc, "subsystem", 3);
    xpc_dictionary_set_uint64(lc, "routine", 0x32f);
    size_t lc_len = 0;
    uint8_t *lc_ser = xpc_wire_serialize_sym(lc, 0x4000032f, &lc_len);
    if (!lc_ser || lc_len < 24) { printf("[!] lc serialize failed\n"); return 1; }

    mach_msg_header_t *hdr0 = (mach_msg_header_t *)(void *)ser;
    size_t env_len = ser_len - 24;
    printf("[*] serialized: total %zu, envelope %zu, id %#x\n",
        ser_len, env_len, hdr0->msgh_id);

    /* mach_voucher_create is a private libsystem_kernel symbol; resolve
     * at runtime so the probe still builds against the public SDK.
     * voucher_mach_msg_set is PUBLIC and is exactly what real libxpc
     * calls before every send — it attaches the thread's effective
     * voucher to the message header. */
    typedef kern_return_t (*vms_fn)(mach_msg_header_t *);
    vms_fn vms = (vms_fn)dlsym(RTLD_DEFAULT, "voucher_mach_msg_set");
    printf("[*] voucher_mach_msg_set = %p\n", (void *)vms);

    typedef kern_return_t (*mvc_fn)(mach_port_t, mach_voucher_attr_recipe_size_t,
        mach_voucher_attr_recipe_t, mach_voucher_attr_recipe_size_t,
        mach_voucher_t *);
    mvc_fn mvc = (mvc_fn)dlsym(RTLD_DEFAULT, "mach_voucher_create");
    mach_port_t voucher = MACH_PORT_NULL;
    mach_voucher_t v = MACH_PORT_NULL;
    if (mvc) {
        kr = mvc(mach_task_self(), 0, NULL, 0, &v);
        if (kr == KERN_SUCCESS) voucher = v;
    }
    printf("[*] minted voucher = %#x (kr %#x%s)\n", voucher, kr,
        mvc ? "" : ", symbol not found");

    int any_answer = 0;
    const char *names[9] = {
        "simple, no descriptor",
        "mach_msg_base_t + descriptor (env @40)",
        "packed descriptor @28 (env @36)",
        "base_t + descriptor + voucher",
        "simple + voucher_mach_msg_set",
        "simple + voucher_mach_msg_set + handle-first keys",
        "simple + MAKE_SEND_ONCE + voucher",
        "simple + MAKE_SEND_ONCE, no voucher",
        "simple + MAKE_SEND_ONCE, launchctl 5-key dict",
    };

    for (int variant = 0; variant < 9; variant++) {
        uint32_t bits;
        mach_port_t vp = MACH_PORT_NULL;
        size_t total;

        /* Complex layout: 24 header + 4 body-count + 4 pad + 8 desc +
         * envelope; packed: 24 + 4 count + 8 desc + envelope. */
        size_t complex_env_off = (variant == 2) ? 36 : 40;

        if (variant == 0 || variant == 4 || variant == 5 ||
            variant == 6 || variant == 7 || variant == 8) {
            mach_msg_type_name_t local = (variant == 6 || variant == 7 ||
                variant == 8)
                ? MACH_MSG_TYPE_MAKE_SEND_ONCE : MACH_MSG_TYPE_MAKE_SEND;
            bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, local);
            if (variant == 8) total = lc_len; else total = 24 + env_len;
            complex_env_off = 24;
        } else {
            switch (variant) {
            case 1:
                bits = MACH_MSGH_BITS_COMPLEX |
                    MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                        MACH_MSG_TYPE_MAKE_SEND);
                break;
            case 2:
                bits = MACH_MSGH_BITS_COMPLEX |
                    MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                        MACH_MSG_TYPE_MAKE_SEND);
                break;
            default:
                bits = MACH_MSGH_BITS_COMPLEX |
                    MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                        MACH_MSG_TYPE_MAKE_SEND) |
                    (MACH_MSG_TYPE_COPY_SEND << 16);
                vp = MACH_PORT_VALID(voucher) ? voucher : MACH_PORT_NULL;
                break;
            }
            total = complex_env_off + env_len;
        }

        uint8_t *buf = calloc(1, total);
        if (!buf) { printf("[!] calloc failed\n"); continue; }
        mach_msg_header_t *m = (mach_msg_header_t *)(void *)buf;

        if (variant == 5) {
            /* Real libxpc's dict htable walked in slot order — the
             * captured request carried "handle" BEFORE "type" even
             * though we inserted type first. Rule out launchd's
             * parse being order-sensitive. */
            xpc_object_t hf = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_uint64(hf, "handle", 0);
            xpc_dictionary_set_uint64(hf, "type", 1);
            size_t hf_len = 0;
            uint8_t *hf_ser = xpc_wire_serialize_sym(hf, 0x4000032f, &hf_len);
            memcpy(buf + complex_env_off, hf_ser + 24, env_len);
            memcpy(m, hf_ser, 24);
            free(hf_ser);
            xpc_release(hf);
        } else if (variant == 8) {
            memcpy(buf + complex_env_off, lc_ser + 24, lc_len - 24);
            memcpy(m, lc_ser, 24);
        } else {
            memcpy(m, ser, 24); /* header fields then fixed below */
        }

        mach_port_t reply_port = MACH_PORT_NULL;
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &reply_port);

        m->msgh_bits = bits;
        m->msgh_size = (mach_msg_size_t)total;
        m->msgh_remote_port = bp;
        m->msgh_local_port = reply_port;
        m->msgh_voucher_port = vp;
        m->msgh_id = 0x4000032f;

        /*
         * Variants 4/5: attach the thread's effective voucher the way
         * real libxpc does — voucher_mach_msg_set() overwrites
         * msgh_voucher_port and ORs the voucher COPY_SEND bits into
         * msgh_bits.
         */
        if ((variant == 4 || variant == 5 || variant == 6) && vms) {
            kern_return_t vkr = vms(m);
            printf("    voucher_mach_msg_set: kr=%#x voucher=0x%x bits=%#x\n",
                vkr, m->msgh_voucher_port, m->msgh_bits);
        }

        if (variant == 1 || variant == 2 || variant == 3) {
            /* descriptor count at offset 24 */
            mach_msg_body_t *body = (mach_msg_body_t *)(void *)(buf + 24);
            body->msgh_descriptor_count = 1;
            size_t desc_off = (variant == 2) ? 28 : 32;
            mach_msg_port_descriptor_t *d =
                (mach_msg_port_descriptor_t *)(void *)(buf + desc_off);
            d->name = bp;
            d->pad1 = 0;
            d->disposition = MACH_MSG_TYPE_COPY_SEND;
            d->type = MACH_MSG_PORT_DESCRIPTOR;
            memcpy(buf + complex_env_off, ser + 24, env_len);
        } else if (variant != 5 && variant != 8) {
            memcpy(buf + 24, ser + 24, env_len);
        }

        printf("[*] variant %d (%s): bits=%#x size=%zu env@%zu\n",
            variant, names[variant], m->msgh_bits, total, complex_env_off);

        kr = mach_msg(m, MACH_SEND_MSG, (mach_msg_size_t)total, 0,
            MACH_PORT_NULL, 0, MACH_PORT_NULL);
        printf("    send:  kr=%#x %s\n", kr, kerr(kr));

        if (kr == KERN_SUCCESS) {
            uint8_t rbuf[65536] __attribute__((aligned(16)));
            mach_msg_header_t *r = (mach_msg_header_t *)(void *)rbuf;
            kr = mach_msg(r, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
                sizeof(rbuf), reply_port, 8000, MACH_PORT_NULL);
            printf("    recv:  kr=%#x %s", kr, kerr(kr));
            if (kr == KERN_SUCCESS && r->msgh_id == 0x20000000) {
                any_answer = 1;
                FILE *rf = fopen("/tmp/v8reply.bin", "wb");
                if (rf) { fwrite(rbuf, 1, r->msgh_size, rf); fclose(rf); }
                const uint8_t *payload = rbuf;
                size_t payload_len = r->msgh_size;
                if (r->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
                    mach_msg_body_t *body =
                        (mach_msg_body_t *)(void *)((uint8_t *)r + 24);
                    mach_msg_descriptor_t *d =
                        (mach_msg_descriptor_t *)(void *)(body + 1);
                    for (mach_msg_size_t i = 0;
                         i < body->msgh_descriptor_count; i++) {
                        if (d->out_of_line.type == MACH_MSG_OOL_DESCRIPTOR) {
                            payload = d->out_of_line.address;
                            payload_len = d->out_of_line.size;
                            printf("\n    OOL payload @%p len=%zu",
                                payload, payload_len);
                        }
                        d = (mach_msg_descriptor_t *)(void *)((uint8_t *)d +
                            sizeof(mach_msg_descriptor_t));
                    }
                }
                xpc_object_t resp =
                    xpc_wire_deserialize_sym(payload, payload_len);
                FILE *of = fopen("/tmp/v8ool.bin", "wb");
                if (of) { fwrite(payload, 1, payload_len, of); fclose(of); }
                if (resp) {
                    char *desc = xpc_copy_description(resp);
                    printf("  reply (%u bytes): %.400s", (unsigned)r->msgh_size, desc);
                    free(desc);
                    xpc_release(resp);
                }
            }
            printf("\n");
        }

        mach_port_mod_refs(mach_task_self(), reply_port,
            MACH_PORT_RIGHT_RECEIVE, -1);
        free(buf);
    }

    xpc_release(req);
    free(ser);
    free(lc_ser);
    xpc_release(lc);
    if (MACH_PORT_VALID(voucher)) {
        mach_port_deallocate(mach_task_self(), voucher);
    }
    printf("[*] %s\n", any_answer ? "GOT AN ANSWER" : "no variant answered");
    return any_answer ? 0 : 1;
}