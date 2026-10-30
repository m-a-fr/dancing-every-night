#include "h264idx.h"
#include <stdlib.h>
#include <string.h>

/* ---- lecture de bits (SPS sans les octets d'échappement 00 00 03) ---- */
typedef struct { const uint8_t *p; size_t n, pos; bool bad; } Bits;

static unsigned get_bit(Bits *b) {
    if (b->pos >= b->n * 8) { b->bad = true; return 0; }
    unsigned v = (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u;
    b->pos++;
    return v;
}
static uint32_t get_bits(Bits *b, int n) {
    uint32_t v = 0;
    while (n-- > 0) v = (v << 1) | get_bit(b);
    return v;
}
static uint32_t get_ue(Bits *b) {
    int zeros = 0;
    while (!get_bit(b) && !b->bad && zeros < 32) zeros++;
    if (zeros >= 32) { b->bad = true; return 0; }
    return (uint32_t)((1u << zeros) - 1u + get_bits(b, zeros));
}
static void skip_se(Bits *b) { get_ue(b); }

/* Remplit les champs de `ix` à partir d'un SPS (octets après l'en-tête de NAL). Faux si illisible. */
static bool parse_sps(const uint8_t *src, size_t n, H264Index *ix) {
    if (n < 4 || n > 512) return false;
    uint8_t buf[512];
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        if (i >= 2 && src[i] == 3 && src[i - 1] == 0 && src[i - 2] == 0) continue;
        buf[m++] = src[i];
    }
    Bits b = { buf, m, 0, false };
    unsigned profile = get_bits(&b, 8);
    ix->profile = (int)profile;
    get_bits(&b, 16);                       /* contraintes + niveau */
    get_ue(&b);                             /* id du SPS */
    switch (profile) {
    case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118: case 128: case 138: case 139: case 134: case 135: {
        unsigned chroma = get_ue(&b);
        if (chroma == 3) get_bit(&b);
        get_ue(&b); get_ue(&b); get_bit(&b);
        if (get_bit(&b)) return false;      /* matrices de quantification : non gérées */
        break;
    }
    default: break;
    }
    get_ue(&b);                             /* log2_max_frame_num */
    unsigned poc = get_ue(&b);
    if (poc == 0) get_ue(&b);
    else if (poc == 1) {
        get_bit(&b); skip_se(&b); skip_se(&b);
        unsigned cycle = get_ue(&b);
        if (cycle > 255) return false;
        for (unsigned i = 0; i < cycle; i++) skip_se(&b);
    }
    ix->num_ref_frames = (int)get_ue(&b);
    get_bit(&b);                            /* écarts autorisés dans frame_num */
    unsigned w_mbs = get_ue(&b) + 1, h_map = get_ue(&b) + 1;
    unsigned frame_mbs_only = get_bit(&b);
    if (!frame_mbs_only) get_bit(&b);
    get_bit(&b);                            /* direct_8x8 */
    unsigned cl = 0, cr = 0, ct = 0, cb = 0;
    if (get_bit(&b)) { cl = get_ue(&b); cr = get_ue(&b); ct = get_ue(&b); cb = get_ue(&b); }
    if (b.bad) return false;
    unsigned h_mbs = h_map * (frame_mbs_only ? 1 : 2);
    unsigned cx = 2, cy = 2 * (frame_mbs_only ? 1 : 2);   /* 4:2:0 */
    ix->width = (int)(w_mbs * 16 - (cl + cr) * cx);
    ix->height = (int)(h_mbs * 16 - (ct + cb) * cy);
    ix->fps = 0.0;
    if (get_bit(&b)) {                      /* paramètres VUI */
        if (get_bit(&b) && get_bits(&b, 8) == 255) get_bits(&b, 32);
        if (get_bit(&b)) get_bit(&b);
        if (get_bit(&b)) { get_bits(&b, 4); if (get_bit(&b)) get_bits(&b, 24); }
        if (get_bit(&b)) { get_ue(&b); get_ue(&b); }
        if (get_bit(&b)) {
            uint32_t units = get_bits(&b, 32), scale = get_bits(&b, 32);
            if (!b.bad && units > 0 && scale > 0) ix->fps = (double)scale / (2.0 * (double)units);
        }
    }
    return !b.bad && ix->width > 0 && ix->height > 0;
}

bool h264_index_build(const uint8_t *data, size_t size, H264Index *out) {
    *out = (H264Index){0};
    int cap = 1024;
    out->au = malloc((size_t)cap * sizeof *out->au);
    if (!out->au) return false;
    bool have_sps = false;
    size_t pending = (size_t)-1;     /* début des NAL sans image (SPS, PPS, SEI, délimiteur) en attente de leur image */

    for (size_t i = 0; i + 4 < size; i++) {
        if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1) continue;
        unsigned type = data[i + 3] & 0x1f;
        size_t start = (i > 0 && data[i - 1] == 0) ? i - 1 : i;     /* code de début sur 4 octets */
        if (type == 1 || type == 5) {
            /* Une image commence par la tranche qui contient le macrobloc 0 (premier bit de l'en-tête = 1) ;
               les tranches suivantes de la même image ne comptent pas. */
            if (data[i + 4] & 0x80) {
                size_t au_start = pending != (size_t)-1 ? pending : start;
                if (out->count > 0) out->au[out->count - 1].size = (uint32_t)(au_start - out->au[out->count - 1].off);
                if (out->count == cap) {
                    cap *= 2;
                    H264Au *g = realloc(out->au, (size_t)cap * sizeof *g);
                    if (!g) { h264_index_free(out); return false; }
                    out->au = g;
                }
                if (out->count == 0) au_start = 0;                  /* les en-têtes d'avant la première image sont à elle */
                out->au[out->count] = (H264Au){ (uint32_t)au_start, 0, false };
                out->count++;
                pending = (size_t)-1;
            } else if (out->count == 0) {
                h264_index_free(out);
                return false;
            }
            if (type == 5) out->au[out->count - 1].key = true;
        } else {
            if (pending == (size_t)-1) pending = start;
            if (type == 7 && !have_sps) {
                size_t e = i + 4;
                while (e + 2 < size && !(data[e] == 0 && data[e + 1] == 0 && data[e + 2] <= 1)) e++;
                have_sps = parse_sps(data + i + 4, e - (i + 4), out);
            }
        }
        i += 2;
    }
    if (out->count > 0) out->au[out->count - 1].size = (uint32_t)(size - out->au[out->count - 1].off);
    if (out->count == 0 || !have_sps || !out->au[0].key) { h264_index_free(out); return false; }
    return true;
}

void h264_index_free(H264Index *ix) {
    free(ix->au);
    *ix = (H264Index){0};
}

int h264_index_key_at(const H264Index *ix, int frame) {
    if (frame >= ix->count) frame = ix->count - 1;
    for (int i = frame; i > 0; i--)
        if (ix->au[i].key) return i;
    return 0;
}
