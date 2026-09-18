/* Standalone storage experiments; not production save code.
 * Build: cc -O2 -Wall -Wextra -std=c11 storage_bench.c -lcrypto -o storage_bench
 * Run: ./storage_bench bench DIR wal-packed 1048576 1000 1
 *      ./storage_bench recover-check DIR
 * Modes: wal-packed, wal-aligned, snapshot-direct, snapshot-replace,
 *        commit-direct, commit-replace.
 * Optional PREALLOC after BATCH: off (default), chunked-64k, chunked-1m.
 * Chunked modes are WAL-only and use fallocate(FALLOC_FL_KEEP_SIZE), reserving
 * one chunk before setup bytes and whole-chunk refills before staged writes.
 * Refills are inside edit-to-durable timing; initial allocation is outside it.
 * Allocation calls/time totals include initial allocation; growth_calls counts
 * refill calls only. Off reports zero allocation metrics. st_blocks observations use
 * POSIX 512-byte units and do not prove physical reservation.
 * BATCH is edits per checked durability group. A transaction latency starts at
 * its encoding and ends at its group's sync completion (includes batch wait).
 * Full-file modes materialize from warm memory, not recovery replay; they are
 * storage-cost baselines, not implementations of durable branching history.
 * recover-check demonstrates the separate retained-base/history Ctrl+S model.
 * Every run creates a fresh private sandbox below DIR; no existing files are
 * opened for writing. Fixtures are owned 0600. Sandboxes are deliberately kept.
 * No permission/xattr preservation, cross-device moves, compaction, adversarial
 * authentication, or physical-power-loss simulation. SHA256 identifies the
 * immutable base; CRC32C detects frame corruption, not hostile modifications.
 * Process exits demonstrate reopen behavior only; unsynced edits are NOT promised
 * durable. Direct overwrite is not crash atomic, even when fsync is checked.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define HDR 40u
#define MAGIC 0x314c4157u
#define MAX_DOC (256u * 1024u * 1024u)
#define MAX_EDITS 100000u
#define MAX_PAYLOAD (1024u * 1024u)
enum { BASE = 1, NODE = 2, HEAD = 3, PAD = 4 };
enum { REC_OK, REC_PREFIX, REC_BAD, REC_BASE_MISMATCH, REC_IO };
typedef struct { unsigned char *p; size_t n; } Blob;
typedef struct {
    uint64_t id, parent, offset;
    uint32_t old_n, new_n;
    unsigned char *bytes;
    size_t result_n;
} Node;
typedef struct {
    Node *nodes;
    size_t count, cap;
    uint64_t head, sequence;
    int status;
} Recovery;
typedef struct { double materialize, encode, write, sync, publish; } Phases;
typedef struct {
    uint64_t chunk_bytes, allocation_calls, growth_calls;
    uint64_t initial_allocation_ns, allocation_ns_total, allocation_ns_max, reserved_bytes;
    int64_t initial_size_before, initial_size_after, initial_blocks_before, initial_blocks_after;
    bool unsupported;
} Allocation;
static size_t short_limit;
static int injected_errno, injected_sync_errno;
static ssize_t fail_after = -1;
static uint64_t bytes_written, file_flushes, dir_flushes;
/* Benchmark WAL groups are encoded into one reusable buffer, then written once.
 * Recovery/fault fixtures use the same framing directly without this buffer. */
static unsigned char *staging;
static size_t staging_len, staging_cap;

static double clock_s(clockid_t id) {
    struct timespec t;
    if (clock_gettime(id, &t)) { perror("clock_gettime"); exit(2); }
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static uint64_t clock_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static int reserve_wal(int fd, uint64_t end, bool initial, Allocation *a) {
    if (end <= a->reserved_bytes) return 0;
    uint64_t chunks = (end - a->reserved_bytes + a->chunk_bytes - 1) / a->chunk_bytes;
    uint64_t length = chunks * a->chunk_bytes;
    if (length > INT64_MAX - a->reserved_bytes) { errno = EOVERFLOW; return -1; }
    uint64_t start = clock_ns();
    int rc;
    do { rc = fallocate(fd, FALLOC_FL_KEEP_SIZE, (off_t)a->reserved_bytes, (off_t)length); }
    while (rc && errno == EINTR);
    int e = errno; uint64_t ns = clock_ns() - start;
    ++a->allocation_calls; if (!initial) ++a->growth_calls;
    if (initial) a->initial_allocation_ns = ns;
    a->allocation_ns_total += ns;
    if (ns > a->allocation_ns_max) a->allocation_ns_max = ns;
    if (!rc) a->reserved_bytes += length;
    else a->unsupported = e == ENOSYS || e == EOPNOTSUPP || e == EINVAL;
    errno = e; return rc;
}
static void print_allocation(const Allocation *a) {
    printf(",\"allocation\":{\"chunk_bytes\":%" PRIu64 ",\"allocation_calls\":%" PRIu64 ",\"growth_calls\":%" PRIu64
           ",\"initial_allocation_ns\":%" PRIu64 ",\"allocation_ns_total\":%" PRIu64 ",\"allocation_ns_max\":%" PRIu64
           ",\"reserved_bytes\":%" PRIu64 ",\"initial_size_before\":%" PRId64 ",\"initial_size_after\":%" PRId64
           ",\"initial_blocks_before\":%" PRId64 ",\"initial_blocks_after\":%" PRId64 "}",
           a->chunk_bytes, a->allocation_calls, a->growth_calls, a->initial_allocation_ns, a->allocation_ns_total,
           a->allocation_ns_max, a->reserved_bytes, a->initial_size_before, a->initial_size_after,
           a->initial_blocks_before, a->initial_blocks_after);
}
static void put32(unsigned char *p, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(x >> (8 * i));
}
static void put64(unsigned char *p, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(x >> (8 * i));
}
static uint32_t get32(const unsigned char *p) {
    uint32_t x = 0; for (unsigned i = 0; i < 4; ++i) x |= (uint32_t)p[i] << (8 * i); return x;
}
static uint64_t get64(const unsigned char *p) {
    uint64_t x = 0; for (unsigned i = 0; i < 8; ++i) x |= (uint64_t)p[i] << (8 * i); return x;
}
static uint32_t crc_table[256];
static void init_crc(void) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (unsigned bit = 0; bit < 8; ++bit)
            c = (c >> 1) ^ (0x82f63b78u & (0u - (c & 1u)));
        crc_table[i] = c;
    }
}
static uint32_t crc_update(uint32_t c, const unsigned char *p, size_t n) {
    while (n--) c = crc_table[(c ^ *p++) & 255u] ^ (c >> 8);
    return c;
}
static uint32_t frame_crc(const unsigned char *h, const unsigned char *p, size_t n) {
    unsigned char copy[HDR]; memcpy(copy, h, HDR); memset(copy + 32, 0, 4);
    return ~crc_update(crc_update(~0u, copy, HDR), p, n);
}
static int write_all(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf;
    while (n) {
        if (fail_after == 0) { errno = injected_errno; return -1; }
        size_t part = n;
        if (short_limit && part > short_limit) part = short_limit;
        if (fail_after > 0 && part > (size_t)fail_after) part = (size_t)fail_after;
        ssize_t got = write(fd, p, part);
        if (got < 0) { if (errno == EINTR) continue; return -1; }
        if (!got) { errno = EIO; return -1; }
        bytes_written += (uint64_t)got;
        if (fail_after > 0) fail_after -= got;
        p += got; n -= (size_t)got;
    }
    return 0;
}
static int sync_file(int fd, bool data_only) {
    if (injected_sync_errno) { errno = injected_sync_errno; return -1; }
    int rc; do { rc = data_only ? fdatasync(fd) : fsync(fd); } while (rc && errno == EINTR);
    if (!rc) ++file_flushes;
    return rc;
}
static int sync_dir(int fd) {
    int rc; do { rc = fsync(fd); } while (rc && errno == EINTR);
    if (!rc) ++dir_flushes;
    return rc;
}
static int read_exact(int fd, void *buf, size_t n, size_t *got) {
    unsigned char *p = buf; *got = 0;
    while (*got < n) {
        ssize_t r = read(fd, p + *got, n - *got);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (!r) break;
        *got += (size_t)r;
    }
    return 0;
}
static Blob load_file(int dir, const char *name) {
    Blob b = {0}; struct stat s;
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return b;
    if (fstat(fd, &s) || s.st_size < 0 || (uint64_t)s.st_size > (uint64_t)MAX_DOC * 2) goto done;
    b.n = (size_t)s.st_size; b.p = malloc(b.n + 1);
    if (b.p) { size_t got; if (read_exact(fd, b.p, b.n, &got) || got != b.n) { free(b.p); b.p = NULL; } }
done:
    close(fd); return b;
}
static int create_file(int dir, const char *name, const void *p, size_t n) {
    int fd = openat(dir, name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    int rc = write_all(fd, p, n);
    if (!rc) rc = sync_file(fd, false);
    int saved = errno; if (close(fd) && !rc) return -1; errno = saved;
    if (!rc) rc = sync_dir(dir);
    return rc;
}
static int sandbox(const char *root, char path[PATH_MAX]) {
    int n = snprintf(path, PATH_MAX, "%s/storage-bench-XXXXXX", root);
    if (n < 0 || n >= PATH_MAX) { errno = ENAMETOOLONG; return -1; }
    if (!mkdtemp(path)) return -1;
    return open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
static void json_string(const char *s) {
    putchar('"'); for (; *s; ++s) { unsigned char c = (unsigned char)*s; if (c == '"' || c == '\\') printf("\\%c", c); else if (c < 32) printf("\\u%04x", c); else putchar(c); } putchar('"');
}
static int report_error(const char *what, const char *path) {
    int e = errno; printf("{\"experiment\":\"storage\",\"status\":\"error\",\"operation\":"); json_string(what);
    printf(",\"errno\":%d,\"error\":", e); json_string(strerror(e)); printf(",\"sandbox\":"); json_string(path); puts("}"); return 1;
}
static int frame(int fd, uint32_t type, uint64_t seq, uint64_t parent,
                 const void *payload, uint32_t n, Phases *ph) {
    double t = clock_s(CLOCK_MONOTONIC);
    unsigned char h[HDR] = {0}; put32(h, MAGIC); put32(h + 4, 1); put32(h + 8, type);
    put32(h + 12, n); put64(h + 16, seq); put64(h + 24, parent);
    put32(h + 36, ~crc_update(~0u, h, 32));
    put32(h + 32, frame_crc(h, payload, n));
    if (staging) {
        if ((size_t)HDR + n > staging_cap - staging_len) { errno = ENOBUFS; return -1; }
        memcpy(staging + staging_len, h, HDR);
        memcpy(staging + staging_len + HDR, payload, n);
        staging_len += HDR + n;
        if (ph) ph->encode += clock_s(CLOCK_MONOTONIC) - t;
        return 0;
    }
    if (ph) ph->encode += clock_s(CLOCK_MONOTONIC) - t;
    t = clock_s(CLOCK_MONOTONIC);
    int rc = write_all(fd, h, HDR); if (!rc) rc = write_all(fd, payload, n);
    if (ph) ph->write += clock_s(CLOCK_MONOTONIC) - t;
    return rc;
}
static int base_frame(int fd, Blob base) {
    unsigned char p[40]; put64(p, base.n); SHA256(base.p, base.n, p + 8);
    return frame(fd, BASE, 1, 0, p, sizeof p, NULL);
}
static int edit_frame(int fd, uint64_t seq, uint64_t parent, uint64_t offset,
                      const void *old, uint32_t old_n, const void *new, uint32_t new_n, Phases *ph) {
    if ((uint64_t)old_n + new_n + 16 > MAX_PAYLOAD) { errno = EOVERFLOW; return -1; }
    double t = clock_s(CLOCK_MONOTONIC);
    size_t n = 16u + (size_t)old_n + new_n;
    unsigned char local[256], *p = n <= sizeof local ? local : malloc(n);
    if (!p) return -1;
    put64(p, offset); put32(p + 8, old_n); put32(p + 12, new_n);
    memcpy(p + 16, old, old_n); memcpy(p + 16 + old_n, new, new_n);
    if (ph) ph->encode += clock_s(CLOCK_MONOTONIC) - t;
    int rc = frame(fd, NODE, seq, parent, p, (uint32_t)n, ph);
    if (p != local) free(p);
    return rc;
}
static int head_frame(int fd, uint64_t seq, uint64_t id, Phases *ph) {
    unsigned char p[8]; put64(p, id); return frame(fd, HEAD, seq, 0, p, sizeof p, ph);
}
static int seal_aligned(int fd, uint64_t *seq, Phases *ph) {
    off_t off = lseek(fd, 0, SEEK_CUR); if (off < 0) return -1;
    size_t total = 4096 - (((size_t)off + staging_len) % 4096);
    if (total < HDR) total += 4096;
    unsigned char zero[4096] = {0};
    return frame(fd, PAD, ++*seq, 0, zero, (uint32_t)(total - HDR), ph);
}
static Node *find_node(Recovery *r, uint64_t id) {
    size_t lo = 0, hi = r->count;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (r->nodes[mid].id < id) lo = mid + 1; else hi = mid; }
    return lo < r->count && r->nodes[lo].id == id ? &r->nodes[lo] : NULL;
}
static void free_recovery(Recovery *r) {
    for (size_t i = 0; i < r->count; ++i) free(r->nodes[i].bytes);
    free(r->nodes); memset(r, 0, sizeof *r);
}
static int apply(Blob *doc, const Node *n) {
    if (n->offset > doc->n || n->old_n > doc->n - (size_t)n->offset ||
        memcmp(doc->p + n->offset, n->bytes, n->old_n)) { errno = EINVAL; return -1; }
    size_t next = doc->n - n->old_n + n->new_n;
    if (next > MAX_DOC) { errno = EOVERFLOW; return -1; }
    if (next > doc->n) { unsigned char *p = realloc(doc->p, next + 1); if (!p) return -1; doc->p = p; }
    if (n->new_n != n->old_n)
        memmove(doc->p + n->offset + n->new_n, doc->p + n->offset + n->old_n, doc->n - (size_t)n->offset - n->old_n);
    memcpy(doc->p + n->offset, n->bytes + n->old_n, n->new_n); doc->n = next;
    return 0;
}
static Blob materialize(Recovery *r, Blob base, uint64_t id) {
    Blob result = {0}; uint64_t *chain = malloc((r->count + 1) * sizeof *chain); size_t n = 0;
    if (!chain) return result;
    while (id) { Node *node = find_node(r, id); if (!node || n >= r->count) goto bad; chain[n++] = id; id = node->parent; }
    result.p = malloc(base.n + 1); if (!result.p) goto bad;
    memcpy(result.p, base.p, base.n); result.n = base.n;
    while (n) if (apply(&result, find_node(r, chain[--n]))) goto bad;
    free(chain); return result;
bad:
    free(chain); free(result.p); result.p = NULL; result.n = 0; return result;
}
static Recovery recover(int dir, const char *name, Blob base) {
    Recovery r = {0}; unsigned char expected[SHA256_DIGEST_LENGTH]; SHA256(base.p, base.n, expected);
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { r.status = REC_IO; return r; }
    for (;;) {
        unsigned char h[HDR]; size_t got;
        if (read_exact(fd, h, HDR, &got)) { r.status = REC_IO; break; }
        if (!got) { if (!r.sequence) r.status = REC_BAD; break; }
        if (got != HDR) { r.status = r.sequence ? REC_PREFIX : REC_BAD; break; }
        uint32_t type = get32(h + 8), n = get32(h + 12); uint64_t seq = get64(h + 16), parent = get64(h + 24);
        if (get32(h) != MAGIC || get32(h + 4) != 1 || get32(h + 36) != ~crc_update(~0u, h, 32) || n > MAX_PAYLOAD || seq != r.sequence + 1 ||
            type < BASE || type > PAD || (type != NODE && parent) ||
            (type == BASE && (seq != 1 || n != 40)) || (seq == 1 && type != BASE) ||
            (type == HEAD && n != 8) || (type == NODE && n < 16)) { r.status = REC_BAD; break; }
        unsigned char *p = malloc((size_t)n + 1);
        if (!p) { r.status = REC_IO; break; }
        if (read_exact(fd, p, n, &got)) { free(p); r.status = REC_IO; break; }
        if (got != n) { free(p); r.status = r.sequence ? REC_PREFIX : REC_BAD; break; }
        if (get32(h + 32) != frame_crc(h, p, n)) { free(p); r.status = REC_BAD; break; }
        if (type == BASE) {
            if (get64(p) != base.n || memcmp(p + 8, expected, sizeof expected)) { free(p); r.status = REC_BASE_MISMATCH; break; }
        } else if (type == NODE) {
            uint64_t offset = get64(p); uint32_t old_n = get32(p + 8), new_n = get32(p + 12);
            Node *ancestor = parent ? find_node(&r, parent) : NULL;
            size_t parent_n = parent && ancestor ? ancestor->result_n : base.n;
            if ((parent && !ancestor) || (uint64_t)16 + old_n + new_n != n || offset > parent_n || old_n > parent_n - offset ||
                parent_n - old_n + (uint64_t)new_n > MAX_DOC || r.count >= MAX_EDITS) { free(p); r.status = REC_BAD; break; }
            if (r.count == r.cap) {
                size_t cap = r.cap ? r.cap * 2 : 32; Node *nodes = realloc(r.nodes, cap * sizeof *nodes);
                if (!nodes) { free(p); r.status = REC_IO; break; } r.nodes = nodes; r.cap = cap;
            }
            memmove(p, p + 16, old_n + new_n);
            r.nodes[r.count++] = (Node){seq, parent, offset, old_n, new_n, p, parent_n - old_n + new_n}; p = NULL;
        } else if (type == HEAD) {
            uint64_t id = get64(p); if (id && !find_node(&r, id)) { free(p); r.status = REC_BAD; break; } r.head = id;
        } else {
            for (uint32_t i = 0; i < n; ++i) if (p[i]) { r.status = REC_BAD; break; }
            if (r.status) { free(p); break; }
        }
        r.sequence = seq; free(p);
    }
    close(fd); return r;
}
static bool equal(Blob a, Blob b) { return a.p && b.p && a.n == b.n && !memcmp(a.p, b.p, a.n); }
static uint64_t random_step(uint64_t *s) { *s ^= *s >> 12; *s ^= *s << 25; *s ^= *s >> 27; return *s * UINT64_C(2685821657736338717); }
static Blob document(size_t n) {
    Blob b = {malloc(n + 1), n}; uint64_t state = UINT64_C(0x8123487182937491);
    if (b.p) for (size_t i = 0; i < n; ++i) b.p[i] = (unsigned char)random_step(&state);
    return b;
}
static int persist_full(int dir, const char *target, Blob doc, bool replace, unsigned group, Phases *ph) {
    char temp[80]; snprintf(temp, sizeof temp, "publish-%u.tmp", group);
    double t = clock_s(CLOCK_MONOTONIC);
    int fd = openat(dir, replace ? temp : target,
                    O_WRONLY | O_CLOEXEC | O_NOFOLLOW | (replace ? O_CREAT | O_EXCL : 0), 0600);
    if (fd < 0) return -1;
    int rc = write_all(fd, doc.p, doc.n);
    if (!rc) rc = ftruncate(fd, (off_t)doc.n);
    ph->write += clock_s(CLOCK_MONOTONIC) - t; t = clock_s(CLOCK_MONOTONIC);
    if (!rc) rc = sync_file(fd, false);
    ph->sync += clock_s(CLOCK_MONOTONIC) - t;
    int e = errno; if (close(fd) && !rc) return -1; errno = e;
    if (!rc && replace) {
        t = clock_s(CLOCK_MONOTONIC); rc = renameat(dir, temp, dir, target); if (!rc) rc = sync_dir(dir);
        ph->publish += clock_s(CLOCK_MONOTONIC) - t;
    }
    return rc;
}
static int compare_double(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double percentile(double *samples, size_t n, unsigned pct) { size_t i = (n * pct + 99) / 100; return samples[i ? i - 1 : 0] * 1e6; }
static int bench(const char *root, const char *mode, size_t doc_n, unsigned iterations, unsigned batch, const char *prealloc) {
    bool wal = !strcmp(mode, "wal-packed") || !strcmp(mode, "wal-aligned");
    bool aligned = !strcmp(mode, "wal-aligned");
    bool replace = !strcmp(mode, "snapshot-replace") || !strcmp(mode, "commit-replace");
    bool commit = !strcmp(mode, "commit-direct") || !strcmp(mode, "commit-replace");
    if (!wal && !replace && !commit && strcmp(mode, "snapshot-direct")) { errno = EINVAL; return report_error("mode", ""); }
    Allocation allocation = {0};
    if (!strcmp(prealloc, "chunked-64k")) allocation.chunk_bytes = 64u * 1024u;
    else if (!strcmp(prealloc, "chunked-1m")) allocation.chunk_bytes = 1024u * 1024u;
    else if (strcmp(prealloc, "off")) { errno = EINVAL; return report_error("preallocation", ""); }
    if (!wal && allocation.chunk_bytes) { errno = EINVAL; return report_error("preallocation requires WAL mode", ""); }
    char path[PATH_MAX] = ""; int dir = sandbox(root, path); if (dir < 0) return report_error("sandbox", path);
    Blob base = document(doc_n), doc = {malloc(doc_n + 1), doc_n};
    double *samples = calloc(iterations, sizeof *samples), *starts = calloc(batch, sizeof *starts);
    int fd = -1; const char *target = commit ? "main" : "draft"; const char *operation = "setup";
    uint64_t wal_end = HDR + 40u;
    if (!base.p || !doc.p || !samples || !starts) goto bad;
    memcpy(doc.p, base.p, doc.n);
    if (create_file(dir, "immutable-base", base.p, base.n) || create_file(dir, target, base.p, base.n)) goto bad;
    if (wal) {
        fd = openat(dir, "history", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
        if (fd < 0) goto bad;
        if (allocation.chunk_bytes) {
            struct stat before, after;
            operation = "initial allocation fstat";
            if (fstat(fd, &before)) goto bad;
            allocation.initial_size_before = before.st_size; allocation.initial_blocks_before = before.st_blocks;
            operation = "initial fallocate KEEP_SIZE";
            if (reserve_wal(fd, allocation.chunk_bytes, true, &allocation)) goto bad;
            operation = "initial allocation fstat";
            if (fstat(fd, &after)) goto bad;
            allocation.initial_size_after = after.st_size; allocation.initial_blocks_after = after.st_blocks;
            if (after.st_size != before.st_size) { errno = EIO; goto bad; }
        }
        operation = "setup";
        if (base_frame(fd, base) || sync_file(fd, true) || sync_dir(dir)) goto bad;
        staging_cap = (size_t)batch * (2 * HDR + 16 + 16 + 8) + 8192;
        staging = malloc(staging_cap);
        if (!staging) goto bad;
    }
    bytes_written = file_flushes = dir_flushes = 0;
    Phases ph = {0}; uint64_t seq = 1, head = 0, state = 17391873;
    unsigned char digest[SHA256_DIGEST_LENGTH] = {0};
    /* Initialize OpenSSL and identify the base outside all measured phases. */
    SHA256(base.p, base.n, digest);
    double cpu = clock_s(CLOCK_PROCESS_CPUTIME_ID), begin = clock_s(CLOCK_MONOTONIC);
    operation = "durable group";
    for (unsigned first = 0, group = 0; first < iterations; ++group) {
        unsigned count = iterations - first; if (count > batch) count = batch;
        staging_len = 0;
        for (unsigned j = 0; j < count; ++j) {
            starts[j] = clock_s(CLOCK_MONOTONIC); double t = starts[j];
            unsigned char old[8], next[8]; size_t off = doc.n / 2 - 4;
            memcpy(old, doc.p + off, sizeof old);
            for (size_t k = 0; k < sizeof next; ++k) next[k] = (unsigned char)random_step(&state);
            memcpy(doc.p + off, next, sizeof next);
            ph.materialize += clock_s(CLOCK_MONOTONIC) - t;
            if (wal) {
                uint64_t id = ++seq;
                if (edit_frame(fd, id, head, off, old, sizeof old, next, sizeof next, &ph) || head_frame(fd, ++seq, id, &ph)) goto bad;
                head = id;
            }
        }
        if (wal) {
            if (aligned && seal_aligned(fd, &seq, &ph)) goto bad;
            if (allocation.chunk_bytes && wal_end + staging_len > allocation.reserved_bytes) {
                operation = "growth fallocate KEEP_SIZE";
                if (reserve_wal(fd, wal_end + staging_len, false, &allocation)) goto bad;
                operation = "durable group";
            }
            double t = clock_s(CLOCK_MONOTONIC);
            if (write_all(fd, staging, staging_len)) goto bad;
            wal_end += staging_len;
            ph.write += clock_s(CLOCK_MONOTONIC) - t;
            t = clock_s(CLOCK_MONOTONIC); if (sync_file(fd, true)) goto bad;
            ph.sync += clock_s(CLOCK_MONOTONIC) - t;
        } else {
            if (persist_full(dir, target, doc, replace, group, &ph)) goto bad;
        }
        double completed = clock_s(CLOCK_MONOTONIC);
        for (unsigned j = 0; j < count; ++j) samples[first + j] = completed - starts[j];
        first += count;
    }
    double elapsed = clock_s(CLOCK_MONOTONIC) - begin; cpu = clock_s(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    uint64_t hot_bytes = bytes_written, hot_flushes = file_flushes, hot_dirs = dir_flushes;
    bool valid; double replay_start = clock_s(CLOCK_MONOTONIC);
    if (wal) {
        struct stat s;
        operation = "logical WAL EOF fstat";
        if (fstat(fd, &s)) goto bad;
        bool logical_eof = s.st_size >= 0 && (uint64_t)s.st_size == wal_end;
        operation = "replay verification";
        if (close(fd)) { fd = -1; goto bad; } fd = -1;
        Recovery r = recover(dir, "history", base); Blob check = materialize(&r, base, r.head);
        valid = logical_eof && r.status == REC_OK && equal(check, doc); free(check.p); free_recovery(&r);
    } else { Blob check = load_file(dir, target); valid = equal(check, doc); free(check.p); }
    double verification = clock_s(CLOCK_MONOTONIC) - replay_start, mean = 0;
    for (unsigned i = 0; i < iterations; ++i) mean += samples[i];
    mean /= iterations; qsort(samples, iterations, sizeof *samples, compare_double);
    printf("{\"experiment\":\"storage-bench\",\"mode\":"); json_string(mode); printf(",\"sandbox\":"); json_string(path);
    printf(",\"preallocation\":"); json_string(prealloc); print_allocation(&allocation);
    printf(",\"status\":\"%s\",\"document_bytes\":%zu,\"iterations\":%u,\"batch\":%u,\"groups\":%u,\"bytes_written\":%" PRIu64 ",\"file_flushes\":%" PRIu64 ",\"directory_flushes\":%" PRIu64,
           valid ? "ok" : "validation-failed", doc_n, iterations, batch, (iterations + batch - 1) / batch, hot_bytes, hot_flushes, hot_dirs);
    printf(",\"latency_us\":{\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f,\"mean\":%.3f},\"wall_seconds\":%.9f,\"cpu_seconds\":%.9f",
           percentile(samples, iterations, 50), percentile(samples, iterations, 95), percentile(samples, iterations, 99), samples[iterations - 1] * 1e6, mean * 1e6, elapsed, cpu);
    printf(",\"phase_seconds\":{\"materialization\":%.9f,\"encoding_checksum\":%.9f,\"data_write\":%.9f,\"file_sync\":%.9f,\"rename_dirsync\":%.9f},\"validation_seconds\":%.9f,\"validation\":%s",
           ph.materialize, ph.encode, ph.write, ph.sync, ph.publish, verification, valid ? "true" : "false");
    printf(",\"validation_kind\":\"%s\",\"durability\":\"%s\",\"retains_branch_history\":%s,\"materialization\":\"warm-memory\",\"latency_definition\":\"edit-start to group durable completion, batch wait included\",\"limitations\":\"owned0600 fixtures; no metadata preservation; full-file modes exclude history pipeline; warm page-cache reopen is not cold-start recovery\"}\n",
           wal ? "fresh-open replay" : "fresh-open exact bytes", wal ? "checked fdatasync, framed prefix recovery" : replace ? "file fsync then rename and directory fsync" : "checked fsync, NOT crash atomic", wal ? "true" : "false");
    close(dir); free(staging); staging = NULL; free(base.p); free(doc.p); free(samples); free(starts); return valid ? 0 : 1;
bad: {
    int e = errno; if (fd >= 0) close(fd); close(dir); free(staging); staging = NULL; free(base.p); free(doc.p); free(samples); free(starts); errno = e;
    printf("{\"experiment\":\"storage-bench\",\"status\":\"error\",\"mode\":"); json_string(mode);
    printf(",\"preallocation\":"); json_string(prealloc); print_allocation(&allocation);
    printf(",\"operation\":"); json_string(operation);
    printf(",\"unsupported\":%s,\"errno\":%d,\"error\":", allocation.unsupported ? "true" : "false", e); json_string(strerror(e));
    printf(",\"sandbox\":"); json_string(path); puts("}"); return allocation.unsupported ? 77 : 1;
}}

/* Recovery checks are finite fixtures; no benchmark numbers are inferred from them. */
typedef struct { unsigned passed, failed; bool first; } Checks;
static void check(Checks *c, const char *name, bool ok) {
    if (!c->first) putchar(',');
    c->first = false;
    printf("{\"name\":"); json_string(name); printf(",\"pass\":%s}", ok ? "true" : "false");
    if (ok) ++c->passed; else ++c->failed;
}
static int select_durable(int fd, uint64_t seq, uint64_t id, uint64_t *ack) {
    if (head_frame(fd, seq, id, NULL) || sync_file(fd, true)) return -1;
    *ack = id; return 0;
}
static bool recovery_matches(int dir, const char *name, Blob base, uint64_t id, Blob expected, int status) {
    Recovery r = recover(dir, name, base); Blob actual = materialize(&r, base, id);
    bool ok = r.status == status && equal(actual, expected); free(actual.p); free_recovery(&r); return ok;
}
static bool fixture_status(int dir, const char *name, const unsigned char *p, size_t n, Blob base, int status, uint64_t head) {
    if (create_file(dir, name, p, n)) return false;
    Recovery r = recover(dir, name, base); bool ok = r.status == status && ((status != REC_PREFIX && status != REC_OK) || r.head == head);
    free_recovery(&r); return ok;
}
static int child_commit(int dir, Blob content, bool replace, unsigned point, const char *target, const char *temp) {
    int fd = openat(dir, replace ? temp : target, O_WRONLY | O_CLOEXEC | (replace ? O_CREAT | O_EXCL : O_TRUNC), 0600);
    if (fd < 0) return 80;
    if (point == 0) _exit(20);
    size_t half = content.n / 2;
    if (write_all(fd, content.p, half)) return 81;
    if (point == 1) _exit(21);
    if (write_all(fd, content.p + half, content.n - half) || ftruncate(fd, (off_t)content.n)) return 82;
    if (point == 2) _exit(22);
    if (sync_file(fd, false)) return 83;
    if (point == 3) _exit(23);
    if (close(fd)) return 84;
    if (replace && renameat(dir, temp, dir, target)) return 85;
    if (point == 4) _exit(24);
    if (replace && sync_dir(dir)) return 86;
    _exit(25);
}
static int recover_check(const char *root) {
    char path[PATH_MAX] = ""; int dir = sandbox(root, path); if (dir < 0) return report_error("sandbox", path);
    Blob base = {(unsigned char *)"0123456789\n", 11};
    Blob a = {(unsigned char *)"01234AA56789\n", 13};
    Blob b = {(unsigned char *)"BB234AA56789\n", 13};
    Blob c = {(unsigned char *)"01CCC4AA56789\n", 14};
    int fd = -1; uint64_t ack = 0;
    if (create_file(dir, "immutable-base", base.p, base.n) || create_file(dir, "main", base.p, base.n)) goto setup_bad;
    fd = openat(dir, "history", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0 || base_frame(fd, base) || edit_frame(fd, 2, 0, 5, "", 0, "AA", 2, NULL) || select_durable(fd, 3, 2, &ack) ||
        edit_frame(fd, 4, 2, 0, "01", 2, "BB", 2, NULL) || select_durable(fd, 5, 4, &ack) || select_durable(fd, 6, 2, &ack) ||
        edit_frame(fd, 7, 2, 2, "23", 2, "CCC", 3, NULL) || select_durable(fd, 8, 7, &ack) || sync_dir(dir)) goto setup_bad;
    if (close(fd)) { fd = -1; goto setup_bad; } fd = -1;
    Blob original = load_file(dir, "history"), disk_base = load_file(dir, "immutable-base");
    if (!original.p || !disk_base.p) { free(original.p); free(disk_base.p); goto setup_bad; }
    printf("{\"experiment\":\"recover-check\",\"sandbox\":"); json_string(path); printf(",\"checks\":[");
    Checks checks = {0, 0, true};
    check(&checks, "immutable-base-reopened", equal(base, disk_base));
    check(&checks, "branch-A-reachable", recovery_matches(dir, "history", disk_base, 2, a, REC_OK));
    check(&checks, "branch-B-reachable-after-undo-and-C", recovery_matches(dir, "history", disk_base, 4, b, REC_OK));
    check(&checks, "branch-C-reachable-after-reopen", recovery_matches(dir, "history", disk_base, 7, c, REC_OK));
    Recovery recovered = recover(dir, "history", disk_base);
    check(&checks, "head-C-durable", recovered.status == REC_OK && recovered.head == 7 && ack == 7); free_recovery(&recovered);
    fd = openat(dir, "history", O_WRONLY | O_APPEND | O_CLOEXEC);
    bool selected = fd >= 0 && select_durable(fd, 9, 4, &ack) == 0;
    if (fd >= 0) { if (close(fd)) selected = false; fd = -1; }
    recovered = recover(dir, "history", disk_base);
    Blob selected_doc = materialize(&recovered, disk_base, recovered.head);
    check(&checks, "durable-select-B-after-recovery", selected && recovered.status == REC_OK && recovered.head == 4 && equal(selected_doc, b));
    free(selected_doc.p); free_recovery(&recovered);
    fd = openat(dir, "history", O_WRONLY | O_APPEND | O_CLOEXEC);
    selected = fd >= 0 && select_durable(fd, 10, 7, &ack) == 0;
    if (fd >= 0) { if (close(fd)) selected = false; fd = -1; }
    recovered = recover(dir, "history", disk_base); selected_doc = materialize(&recovered, disk_base, recovered.head);
    check(&checks, "durable-select-C-after-recovery", selected && recovered.status == REC_OK && recovered.head == 7 && equal(selected_doc, c));
    free(selected_doc.p); free_recovery(&recovered);

    /* Original ends with HEAD(8). Tear its header/payload, preserving prior A head. */
    size_t last = original.n - (HDR + 8); size_t cuts[] = {1, 4, 15, 31, 39, 40, 41, 47};
    for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; ++i) {
        char name[80]; snprintf(name, sizeof name, "partial-final-%zu", cuts[i]);
        check(&checks, name, fixture_status(dir, name, original.p, last + cuts[i], disk_base, REC_PREFIX, 2));
    }
    /* Also tear a NODE payload and distinguish clean EOF from an incomplete tail. */
    size_t node_c = last - (HDR + 16 + 2 + 3);
    check(&checks, "partial-node-payload-prefix", fixture_status(dir, "partial-node", original.p, node_c + HDR + 17, disk_base, REC_PREFIX, 2));
    check(&checks, "complete-record-prefix-clean-eof", fixture_status(dir, "clean-prefix", original.p, node_c, disk_base, REC_OK, 2));
    unsigned char *mutated = malloc(original.n);
    if (!mutated) check(&checks, "corruption-fixture-allocation", false);
    else {
        memcpy(mutated, original.p, original.n); mutated[HDR + 8] ^= 1;
        check(&checks, "middle-checksum-corruption-rejected", fixture_status(dir, "bad-crc", mutated, original.n, disk_base, REC_BAD, 0));
        memcpy(mutated, original.p, original.n); put32(mutated + 80 + 12, UINT32_MAX);
        check(&checks, "oversized-length-rejected", fixture_status(dir, "bad-length", mutated, original.n, disk_base, REC_BAD, 0));
        memcpy(mutated, original.p, original.n); put32(mutated + 80 + 12, 10000);
        check(&checks, "middle-length-corruption-not-incomplete-tail", fixture_status(dir, "bad-length-tail", mutated, original.n, disk_base, REC_BAD, 0));
        memcpy(mutated, original.p, original.n); put64(mutated + 80 + 16, 99);
        put32(mutated + 80 + 36, ~crc_update(~0u, mutated + 80, 32));
        put32(mutated + 80 + 32, frame_crc(mutated + 80, mutated + 80 + HDR, get32(mutated + 80 + 12)));
        check(&checks, "valid-checksum-wrong-sequence-rejected", fixture_status(dir, "bad-sequence", mutated, original.n, disk_base, REC_BAD, 0));
        memcpy(mutated, original.p, original.n); put64(mutated + 80 + HDR, MAX_DOC);
        put32(mutated + 80 + 32, frame_crc(mutated + 80, mutated + 80 + HDR, get32(mutated + 80 + 12)));
        check(&checks, "valid-checksum-invalid-edit-bounds-rejected", fixture_status(dir, "bad-bounds", mutated, original.n, disk_base, REC_BAD, 0));
        memcpy(mutated, original.p, original.n); mutated[last + HDR] ^= 1;
        check(&checks, "complete-corrupt-tail-rejected-not-prefix", fixture_status(dir, "bad-tail", mutated, original.n, disk_base, REC_BAD, 0));
        free(mutated);
    }
    Blob changed = {(unsigned char *)"x123456789\n", 11}; recovered = recover(dir, "history", changed);
    check(&checks, "external-baseline-mismatch-rejected", recovered.status == REC_BASE_MISMATCH); free_recovery(&recovered);
    short_limit = 3; bool short_ok = create_file(dir, "short-writes", original.p, original.n) == 0; short_limit = 0;
    check(&checks, "forced-three-byte-short-writes-recovered", short_ok && recovery_matches(dir, "short-writes", disk_base, 7, c, REC_OK));
    for (unsigned i = 0; i < 3; ++i) {
        char name[80]; snprintf(name, sizeof name, "injected-%s", i == 0 ? "EIO-write" : i == 1 ? "ENOSPC-write" : "EIO-sync");
        bool ready = create_file(dir, name, original.p, original.n) == 0;
        fd = ready ? openat(dir, name, O_WRONLY | O_APPEND | O_CLOEXEC) : -1;
        uint64_t durable = 7; int rc = 0, e = 0;
        if (fd >= 0) {
            if (i < 2) { fail_after = 13; injected_errno = i == 0 ? EIO : ENOSPC; } else injected_sync_errno = EIO;
            rc = select_durable(fd, 9, 4, &durable); e = errno;
            fail_after = -1; injected_sync_errno = injected_errno = 0;
            if (close(fd)) ready = false;
            fd = -1;
        }
        check(&checks, name, ready && rc == -1 && e == (i == 1 ? ENOSPC : EIO) && durable == 7);
        recovered = recover(dir, name, disk_base);
        snprintf(name, sizeof name, "error-reopen-%u", i);
        check(&checks, name, i < 2 ? recovered.status == REC_PREFIX && recovered.head == 7 : recovered.status == REC_OK && recovered.head == 4);
        free_recovery(&recovered);
    }
    /* Each process-exit fixture gets a separate main filename. Recovery always
     * reads immutable-base + history, NEVER whatever main currently contains. */
    for (unsigned replace = 0; replace < 2; ++replace) for (unsigned point = 0; point < 6; ++point) {
        char target[80], temp[80], name[100];
        snprintf(target, sizeof target, "exit-main-%u-%u", replace, point); snprintf(temp, sizeof temp, "exit-temp-%u-%u", replace, point);
        bool ok = create_file(dir, target, base.p, base.n) == 0; pid_t pid = ok ? fork() : -1;
        if (pid == 0) _exit(child_commit(dir, c, replace != 0, point, target, temp));
        int status = 0; pid_t waited = -1;
        if (pid > 0) { do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR); }
        ok = ok && waited == pid && pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == (int)(20 + point);
        Blob main = load_file(dir, target);
        if (replace) ok = ok && equal(main, point < 4 ? base : c);
        else if (point == 0) ok = ok && main.p && main.n == 0;
        else if (point == 1) ok = ok && main.p && main.n == c.n / 2 && !memcmp(main.p, c.p, main.n);
        else ok = ok && equal(main, c);
        free(main.p);
        recovered = recover(dir, "history", disk_base); Blob actual = materialize(&recovered, disk_base, recovered.head);
        ok = ok && recovered.status == REC_OK && recovered.head == 7 && equal(actual, c);
        free(actual.p); free_recovery(&recovered);
        snprintf(name, sizeof name, "process-exit-%s-checkpoint-%u-independent-replay", replace ? "replace" : "direct", point);
        check(&checks, name, ok);
    }
    printf("],\"passed\":%u,\"failed\":%u,\"validation\":%s,"
           "\"model\":\"versioned independently header-checked CRC32C frames; monotonic sequence; bounded edits retaining removed+inserted bytes and parent IDs; SHA256 immutable-base identity; synced HEAD selection; original/private base and WAL retained across Ctrl+S\","
           "\"partial_tail\":\"explicit recovered-prefix status; complete corrupt frames and corrupted length headers rejected; no resynchronization past corruption\","
           "\"acknowledgment\":\"durable revision acknowledged only after checked sync; failed sync may still leave readable unacknowledged data\","
           "\"checkpoint_map\":{\"0\":\"opened/truncated or created temp\",\"1\":\"half written\",\"2\":\"full write before sync\",\"3\":\"file sync complete\",\"4\":\"rename complete (direct: close)\",\"5\":\"directory sync complete (direct: close)\"},"
           "\"limitations\":\"fork/_exit checkpoints, NOT physical power-loss simulation; warm page-cache reopen; owned0600 fixtures; no metadata or cross-device generalization\"}\n",
           checks.passed, checks.failed, checks.failed ? "false" : "true");
    free(original.p); free(disk_base.p); close(dir); return checks.failed ? 1 : 0;
setup_bad: {
    int e = errno; if (fd >= 0) close(fd); close(dir); errno = e; return report_error("recovery fixture setup", path);
}}
static bool number(const char *s, uint64_t min, uint64_t max, uint64_t *out) {
    char *end; errno = 0; if (!*s || *s == '-') return false;
    unsigned long long n = strtoull(s, &end, 10); if (errno || *end || n < min || n > max) return false;
    *out = n; return true;
}
int main(int argc, char **argv) {
    init_crc();
    if (argc == 3 && !strcmp(argv[1], "recover-check")) return recover_check(argv[2]);
    if ((argc == 6 || argc == 7 || argc == 8) && !strcmp(argv[1], "bench")) {
        uint64_t n, iterations, batch = 1;
        if (number(argv[4], 16, MAX_DOC, &n) && number(argv[5], 1, MAX_EDITS, &iterations) &&
            (argc == 6 || number(argv[6], 1, iterations, &batch))) return bench(argv[2], argv[3], (size_t)n, (unsigned)iterations, (unsigned)batch, argc == 8 ? argv[7] : "off");
    }
    fputs("usage: storage_bench bench DIR MODE DOCUMENT_BYTES ITERATIONS [BATCH [PREALLOC]]\n"
          "       storage_bench recover-check DIR\n"
          "MODE: wal-packed wal-aligned snapshot-direct snapshot-replace commit-direct commit-replace\n"
          "PREALLOC: off (default), chunked-64k, chunked-1m; chunked allocation requires WAL mode\n"
          "bounds: document 16..268435456 bytes, iterations 1..100000, batch 1..iterations\n", stderr);
    return 2;
}
