/*
 * Standalone durable QD1 I/O experiment; not an application file API.
 * Build: cc -O2 -std=c11 -Wall -Wextra -pthread prototypes/saving/io_bench.c \
 *           -luring -lrt -o /tmp/io_bench
 * Run: /tmp/io_bench DIR sync|worker|worker-ring|aio|uring|uring-linked RECORD_BYTES RECORDS_PER_SYNC \
 *           ITERATIONS off|keep-size|chunked-64k|chunked-1m [PACE_US]
 * Check: /tmp/io_bench DIR queue-check
 * Example: /tmp/io_bench /path/to/disposable-dir worker 256 16 1000 off 1000
 * Exit 0: verified; 77: unsupported; 1: runtime failure; 2: bad arguments.
 * Only this process's mkstemp file is removed. No fallback for unavailable I/O.
 * Warmup: eight durable transactions, included in the 256 MiB file limit.
 * Every record starts with its unique 64-bit record number; remaining bytes
 * are deterministic mixed data, not repeated zero pages. Minimum record: 8 B.
 * Submission is admission, not a promise of nonblocking behavior. For sync,
 * submission includes execution and equals durable latency. The benchmark is
 * QD1 for every engine; queue-check separately exercises ring depth up to 64.
 * Buffers remain immutable until their individual completions are observed.
 * write_complete_ns measures observed write completion, not durability. With
 * linked requests the following flush may already be running when observed.
 * CPU/wall cover the measured loop including generation and optional pacing;
 * latency samples exclude both. Setup, warmup, verification are excluded.
 */
#define _GNU_SOURCE
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <liburing.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <linux/futex.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define MAX_FILE_BYTES (UINT64_C(256) * 1024 * 1024)
#define MAX_BATCH_BYTES (UINT64_C(16) * 1024 * 1024)
#define MAX_ITERATIONS UINT64_C(1000000)
#define MAX_PACE_US UINT64_C(1000000)
#define MAX_PACED_US UINT64_C(600000000)
#define WARMUP 8U

enum engine_kind { ENGINE_SYNC, ENGINE_WORKER, ENGINE_WORKER_RING, ENGINE_AIO, ENGINE_URING, ENGINE_URING_LINKED };
struct request { const unsigned char *data; size_t size; off_t offset; };
struct allocation {
    uint64_t chunk_bytes, allocation_calls, growth_calls, initial_allocation_ns;
    uint64_t allocation_ns_total, allocation_ns_max, reserved_bytes;
    int64_t initial_size_before, initial_size_after, initial_blocks_before, initial_blocks_after;
    bool failed;
};
#define WORKER_RING_SLOTS 64U
struct completion {
    _Atomic uint32_t done;
    int error;
    uint64_t write_complete;
};
struct worker_slot { struct request request; struct completion *completion; };
/* Check-only gates make saturation deterministic, without slowing normal I/O
 * with another thread or relying on sleeps to arrange a particular schedule. */
struct queue_probe {
    _Atomic uint32_t gate, full, idle, event;
};
struct engine {
    enum engine_kind kind;
    int fd;
    struct allocation allocation;
    _Alignas(64) _Atomic uint64_t submitted;
    _Atomic uint32_t request_event, request_waiting, stopping;
    _Alignas(64) _Atomic uint64_t completed;
    _Atomic uint32_t completion_event, completion_waiting;
    _Alignas(64) struct worker_slot slots[WORKER_RING_SLOTS];
    struct queue_probe *probe;
    struct io_uring ring;
    bool ring_ready, mutex_ready, request_cv_ready, done_cv_ready, thread_ready;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t request_cv, done_cv;
    bool pending, done, stop;
    struct request request;
    int worker_error;
    uint64_t worker_write_complete;
};
struct stats { uint64_t p50, p95, p99, max; long double mean; };

static int now_ns(clockid_t clock, uint64_t *out)
{
    struct timespec t;
    if (clock_gettime(clock, &t) != 0) return errno;
    *out = (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
    return 0;
}

static int parse_number(const char *s, uint64_t *value)
{
    if (!s[0]) return EINVAL;
    for (const char *p = s; *p; ++p) if (*p < '0' || *p > '9') return EINVAL;
    errno = 0;
    char *end;
    unsigned long long n = strtoull(s, &end, 10);
    if (errno || *end) return EINVAL;
    *value = (uint64_t)n;
    return 0;
}

static uint64_t mix64(uint64_t x)
{
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static void fill_batch(unsigned char *data, size_t record_bytes,
                       size_t records, uint64_t first_record)
{
    for (size_t r = 0; r < records; ++r) {
        uint64_t id = first_record + r;
        unsigned char *p = data + r * record_bytes;
        for (size_t j = 0; j < record_bytes; j += 8) {
            uint64_t v = j == 0 ? id : mix64(id ^ mix64((uint64_t)j));
            size_t n = record_bytes - j < 8 ? record_bytes - j : 8;
            for (size_t k = 0; k < n; ++k) p[j + k] = (unsigned char)(v >> (k * 8));
        }
    }
}

/* Metrics cover setup plus warmup plus measured transactions. KEEP_SIZE must
 * preserve logical EOF; st_blocks observations are in POSIX 512-byte units. */
static int reserve_to(struct engine *e, uint64_t end, bool initial)
{
    struct allocation *a = &e->allocation;
    uint64_t start_ns, end_ns;
    int error = now_ns(CLOCK_MONOTONIC, &start_ns);
    if (error) return error;
    ++a->allocation_calls;
    if (!initial) ++a->growth_calls;
    int result;
    do {
        result = fallocate(e->fd, FALLOC_FL_KEEP_SIZE, (off_t)a->reserved_bytes,
                           (off_t)(end - a->reserved_bytes));
    } while (result != 0 && errno == EINTR);
    int allocation_error = result == 0 ? 0 : errno;
    error = now_ns(CLOCK_MONOTONIC, &end_ns);
    if (!error) {
        uint64_t ns = end_ns - start_ns;
        a->allocation_ns_total += ns;
        if (ns > a->allocation_ns_max) a->allocation_ns_max = ns;
        if (initial) a->initial_allocation_ns = ns;
    }
    if (!allocation_error) a->reserved_bytes = end;
    else { a->failed = true; return allocation_error; }
    return error;
}

static int reserve_initial(struct engine *e, const char *mode, uint64_t file_bytes)
{
    if (!strcmp(mode, "off")) return 0;
    struct allocation *a = &e->allocation;
    a->chunk_bytes = !strcmp(mode, "chunked-64k") ? 65536 :
                     !strcmp(mode, "chunked-1m") ? 1048576 : 0;
    struct stat st;
    if (fstat(e->fd, &st) != 0) return errno;
    a->initial_size_before = st.st_size;
    a->initial_blocks_before = st.st_blocks;
    int error = reserve_to(e, a->chunk_bytes ? a->chunk_bytes : file_bytes, true);
    if (fstat(e->fd, &st) != 0) return error ? error : errno;
    a->initial_size_after = st.st_size;
    a->initial_blocks_after = st.st_blocks;
    if (!error && a->initial_size_after != a->initial_size_before) error = EIO;
    return error;
}

static int reserve_request(struct engine *e, const struct request *r)
{
    struct allocation *a = &e->allocation;
    if (!a->chunk_bytes) return 0;
    uint64_t end = (uint64_t)r->offset + r->size;
    if (end <= a->reserved_bytes) return 0;
    uint64_t chunks = (end + a->chunk_bytes - 1) / a->chunk_bytes;
    return reserve_to(e, chunks * a->chunk_bytes, false);
}

static int write_sync(int fd, const struct request *r, uint64_t *write_complete)
{
    size_t done = 0;
    while (done < r->size) {
        ssize_t n = pwrite(fd, r->data + done, r->size - done, r->offset + (off_t)done);
        if (n < 0) { if (errno == EINTR) continue; return errno; }
        if (n == 0) return EIO;
        done += (size_t)n;
    }
    int timing_error = now_ns(CLOCK_MONOTONIC, write_complete);
    while (fdatasync(fd) != 0) { if (errno != EINTR) return errno; }
    return timing_error;
}

/* Mutex/condition failures on valid initialized objects are unrecoverable;
 * do not pretend a request completed or strand a waiter on such a failure. */
static void thread_check(int error)
{
    if (error) {
        fprintf(stderr, "pthread synchronization failure: %s\n", strerror(error));
        abort();
    }
}

/* Linux futexes require aligned, lock-free 32-bit words, not a libatomic lock. */
_Static_assert(sizeof(_Atomic uint32_t) == 4, "futex word size");

static void event_wake(_Atomic uint32_t *word)
{
    if (syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0) < 0) {
        perror("futex wake");
        abort();
    }
}

static void event_signal(_Atomic uint32_t *word)
{
    atomic_fetch_add_explicit(word, 1, memory_order_release);
    event_wake(word);
}

static void worker_signal(_Atomic uint32_t *word, _Atomic uint32_t *waiting)
{
    atomic_fetch_add_explicit(word, 1, memory_order_seq_cst);
    if (atomic_load_explicit(waiting, memory_order_seq_cst)) event_wake(word);
}

static void event_wait(_Atomic uint32_t *word, uint32_t expected)
{
    if (syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0) < 0 &&
        errno != EAGAIN && errno != EINTR) {
        perror("futex wait");
        abort();
    }
}

/* Ring waiters register before snapshotting the event and rechecking their
 * predicate. Notification increments the event before checking registration.
 * These registration/event operations are seq_cst: if notification misses
 * registration, the waiter sees the notification on its subsequent recheck.
 * Otherwise the publisher wakes it, or the changed futex word yields EAGAIN.
 * EINTR/spurious returns recheck. Only waiters cause a wake syscall; an active
 * consumer can drain the ring without per-request producer wake syscalls.
 * Check-only probes use unconditional event_signal and snapshot/recheck loops.
 * The finite workload is < 2^32 notifications, so sequence wrap cannot ABA.
 *
 * submitted release/acquire publishes immutable request + completion pointer.
 * completed release/acquire releases the slot only after its request is done.
 * completion.done release/acquire separately releases caller-owned data and
 * publishes error/timestamp. Callers retain both objects until done, including
 * on errors. No slot or request allocation occurs on the handoff path. */
static bool worker_ring_try_submit(struct engine *e, const struct request *r,
                                   struct completion *c)
{
    uint64_t head = atomic_load_explicit(&e->submitted, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&e->completed, memory_order_acquire);
    if (head - tail == WORKER_RING_SLOTS) return false;
    atomic_store_explicit(&c->done, 0, memory_order_relaxed);
    e->slots[head % WORKER_RING_SLOTS] = (struct worker_slot) { *r, c };
    atomic_store_explicit(&e->submitted, head + 1, memory_order_release);
    worker_signal(&e->request_event, &e->request_waiting);
    return true;
}

static void worker_ring_submit(struct engine *e, const struct request *r,
                               struct completion *c)
{
    if (worker_ring_try_submit(e, r, c)) return;
    atomic_store_explicit(&e->completion_waiting, 1, memory_order_seq_cst);
    for (;;) {
        uint32_t seq = atomic_load_explicit(&e->completion_event, memory_order_seq_cst);
        if (worker_ring_try_submit(e, r, c)) break;
        if (e->probe) {
            atomic_fetch_add_explicit(&e->probe->full, 1, memory_order_release);
            event_signal(&e->probe->event);
        }
        event_wait(&e->completion_event, seq);
    }
    atomic_store_explicit(&e->completion_waiting, 0, memory_order_seq_cst);
}

static int worker_ring_wait(struct engine *e, struct completion *c, uint64_t *written)
{
    if (!atomic_load_explicit(&c->done, memory_order_acquire)) {
        atomic_store_explicit(&e->completion_waiting, 1, memory_order_seq_cst);
        for (;;) {
            uint32_t seq = atomic_load_explicit(&e->completion_event, memory_order_seq_cst);
            if (atomic_load_explicit(&c->done, memory_order_acquire)) break;
            event_wait(&e->completion_event, seq);
        }
        atomic_store_explicit(&e->completion_waiting, 0, memory_order_seq_cst);
    }
    *written = c->write_complete;
    return c->error;
}

static void *worker_ring_main(void *arg)
{
    struct engine *e = arg;
    if (e->probe) {
        while (!atomic_load_explicit(&e->probe->gate, memory_order_acquire))
            event_wait(&e->probe->gate, 0);
    }
    uint64_t tail = 0;
    for (;;) {
        uint64_t head = atomic_load_explicit(&e->submitted, memory_order_acquire);
        if (tail == head) {
            atomic_store_explicit(&e->request_waiting, 1, memory_order_seq_cst);
            uint32_t seq = atomic_load_explicit(&e->request_event, memory_order_seq_cst);
            head = atomic_load_explicit(&e->submitted, memory_order_acquire);
            bool stop = atomic_load_explicit(&e->stopping, memory_order_acquire);
            if (tail == head && !stop) {
                if (e->probe) {
                    atomic_fetch_add_explicit(&e->probe->idle, 1, memory_order_release);
                    event_signal(&e->probe->event);
                }
                event_wait(&e->request_event, seq);
            }
            atomic_store_explicit(&e->request_waiting, 0, memory_order_seq_cst);
            if (tail == head && stop) break;
            continue;
        }
        struct worker_slot slot = e->slots[tail % WORKER_RING_SLOTS];
        uint64_t written = 0;
        int error = reserve_request(e, &slot.request);
        if (!error) error = write_sync(e->fd, &slot.request, &written);
        slot.completion->error = error;
        slot.completion->write_complete = written;
        atomic_store_explicit(&slot.completion->done, 1, memory_order_release);
        atomic_store_explicit(&e->completed, ++tail, memory_order_release);
        worker_signal(&e->completion_event, &e->completion_waiting);
    }
    return NULL;
}

static void *worker_main(void *arg)
{
    struct engine *e = arg;
    thread_check(pthread_mutex_lock(&e->mutex));
    for (;;) {
        while (!e->pending && !e->stop)
            thread_check(pthread_cond_wait(&e->request_cv, &e->mutex));
        if (e->stop) break;
        struct request r = e->request;
        e->pending = false;
        thread_check(pthread_mutex_unlock(&e->mutex));
        uint64_t write_complete = 0;
        int error = reserve_request(e, &r);
        if (!error) error = write_sync(e->fd, &r, &write_complete);
        thread_check(pthread_mutex_lock(&e->mutex));
        e->worker_error = error;
        e->worker_write_complete = write_complete;
        e->done = true;
        thread_check(pthread_cond_signal(&e->done_cv));
    }
    thread_check(pthread_mutex_unlock(&e->mutex));
    return NULL;
}

static int engine_init(struct engine *e)
{
    int error;
    if (e->kind == ENGINE_URING || e->kind == ENGINE_URING_LINKED) {
        error = io_uring_queue_init(4, &e->ring, 0); /* no SQPOLL */
        if (error < 0) return -error;
        e->ring_ready = true;
    } else if (e->kind == ENGINE_WORKER) {
        if ((error = pthread_mutex_init(&e->mutex, NULL))) return error;
        e->mutex_ready = true;
        if ((error = pthread_cond_init(&e->request_cv, NULL))) return error;
        e->request_cv_ready = true;
        if ((error = pthread_cond_init(&e->done_cv, NULL))) return error;
        e->done_cv_ready = true;
        if ((error = pthread_create(&e->thread, NULL, worker_main, e))) return error;
        e->thread_ready = true;
    } else if (e->kind == ENGINE_WORKER_RING) {
        if (!atomic_is_lock_free(&e->submitted) || !atomic_is_lock_free(&e->completed) ||
            !atomic_is_lock_free(&e->request_event) || !atomic_is_lock_free(&e->completion_event))
            return ENOTSUP;
        if ((error = pthread_create(&e->thread, NULL, worker_ring_main, e))) return error;
        e->thread_ready = true;
    }
    return 0;
}

static int engine_destroy(struct engine *e)
{
    if (e->thread_ready) {
        if (e->kind == ENGINE_WORKER_RING) {
            /* Only the producer destroys; all admitted work is drained first. */
            atomic_store_explicit(&e->stopping, 1, memory_order_release);
            event_signal(&e->request_event);
        } else {
            thread_check(pthread_mutex_lock(&e->mutex));
            e->stop = true;
            thread_check(pthread_cond_signal(&e->request_cv));
            thread_check(pthread_mutex_unlock(&e->mutex));
        }
        thread_check(pthread_join(e->thread, NULL));
        e->thread_ready = false;
    }
    if (e->done_cv_ready) thread_check(pthread_cond_destroy(&e->done_cv));
    if (e->request_cv_ready) thread_check(pthread_cond_destroy(&e->request_cv));
    if (e->mutex_ready) thread_check(pthread_mutex_destroy(&e->mutex));
    if (e->ring_ready) io_uring_queue_exit(&e->ring);
    return 0;
}

/* Always reap a submitted aiocb, even when aio_suspend fails. */
static int aio_wait(struct aiocb *cb, ssize_t *result)
{
    const struct aiocb *list[] = { cb };
    int wait_error = 0, status;
    while ((status = aio_error(cb)) == EINPROGRESS) {
        if (aio_suspend(list, 1, NULL) != 0 && errno != EINTR) {
            if (!wait_error) wait_error = errno;
            /* Completion is still required before this control block can die. */
        }
    }
    int status_error = status < 0 ? errno : status;
    *result = aio_return(cb);
    if (status_error) return status_error;
    if (*result < 0) return errno ? errno : EIO;
    return wait_error;
}

static int aio_transaction(int fd, const struct request *r, uint64_t *admitted,
                           uint64_t *write_complete)
{
    size_t done = 0;
    int timing_error = 0;
    bool submitted = false;
    while (done < r->size) {
        struct aiocb cb;
        memset(&cb, 0, sizeof(cb));
        cb.aio_fildes = fd;
        cb.aio_buf = (void *)(r->data + done);
        cb.aio_nbytes = r->size - done;
        cb.aio_offset = r->offset + (off_t)done;
        cb.aio_sigevent.sigev_notify = SIGEV_NONE;
        while (aio_write(&cb) != 0) { if (errno != EINTR) return errno; }
        if (!submitted) {
            timing_error = now_ns(CLOCK_MONOTONIC, admitted);
            submitted = true;
        }
        ssize_t n;
        int error = aio_wait(&cb, &n);
        if (error == EINTR) continue;
        if (error) return error;
        if (n <= 0 || (size_t)n > r->size - done) return EIO;
        done += (size_t)n;
    }
    int write_time_error = now_ns(CLOCK_MONOTONIC, write_complete);
    if (!timing_error) timing_error = write_time_error;
    struct aiocb cb;
    memset(&cb, 0, sizeof(cb));
    cb.aio_fildes = fd;
    cb.aio_sigevent.sigev_notify = SIGEV_NONE;
    while (aio_fsync(O_DSYNC, &cb) != 0) { if (errno != EINTR) return errno; }
    for (;;) {
        ssize_t n;
        int error = aio_wait(&cb, &n);
        if (error == EINTR) {
            memset(&cb, 0, sizeof(cb));
            cb.aio_fildes = fd;
            cb.aio_sigevent.sigev_notify = SIGEV_NONE;
            while (aio_fsync(O_DSYNC, &cb) != 0) { if (errno != EINTR) return errno; }
            continue;
        }
        if (error) return error;
        if (n != 0) return EIO;
        return timing_error;
    }
}

static int ring_submit(struct engine *e)
{
    int n;
    do { n = io_uring_submit(&e->ring); } while (n == -EINTR);
    return n < 0 ? -n : n == 1 ? 0 : EIO;
}

static int ring_wait(struct engine *e, int *result)
{
    struct io_uring_cqe *cqe;
    int error;
    do { error = io_uring_wait_cqe(&e->ring, &cqe); } while (error == -EINTR);
    if (error < 0) return -error;
    *result = cqe->res;
    io_uring_cqe_seen(&e->ring, cqe);
    return 0;
}

static int ring_transaction(struct engine *e, const struct request *r, uint64_t *admitted,
                            uint64_t *write_complete)
{
    size_t done = 0;
    int timing_error = 0;
    bool submitted = false;
    while (done < r->size) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&e->ring);
        if (!sqe) return EBUSY;
        io_uring_prep_write(sqe, e->fd, r->data + done,
                           (unsigned)(r->size - done), r->offset + (off_t)done);
        int error = ring_submit(e);
        if (error) return error;
        if (!submitted) {
            timing_error = now_ns(CLOCK_MONOTONIC, admitted);
            submitted = true;
        }
        int n;
        if ((error = ring_wait(e, &n))) return error;
        if (n == -EINTR) continue;
        if (n < 0) return -n;
        if (!n || (size_t)n > r->size - done) return EIO;
        done += (size_t)n;
    }
    int write_time_error = now_ns(CLOCK_MONOTONIC, write_complete);
    if (!timing_error) timing_error = write_time_error;
    for (;;) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&e->ring);
        if (!sqe) return EBUSY;
        io_uring_prep_fsync(sqe, e->fd, IORING_FSYNC_DATASYNC);
        int error = ring_submit(e), n;
        if (error) return error;
        if ((error = ring_wait(e, &n))) return error;
        if (n == -EINTR) continue;
        if (n < 0) return -n;
        if (n != 0) return EIO;
        return timing_error;
    }
}

/* One submission for the dependent pair, but both results remain mandatory.
 * A short/error write may cancel its linked flush; retry only after reaping
 * both requests. Never acknowledge a short write or a failed flush. */
static int ring_linked_transaction(struct engine *e, const struct request *r,
                                   uint64_t *admitted, uint64_t *write_complete)
{
    struct io_uring_sqe *write_sqe = io_uring_get_sqe(&e->ring);
    struct io_uring_sqe *sync_sqe = io_uring_get_sqe(&e->ring);
    if (!write_sqe || !sync_sqe) return EBUSY;
    io_uring_prep_write(write_sqe, e->fd, r->data, (unsigned)r->size, r->offset);
    write_sqe->flags |= IOSQE_IO_LINK;
    io_uring_sqe_set_data64(write_sqe, 1);
    io_uring_prep_fsync(sync_sqe, e->fd, IORING_FSYNC_DATASYNC);
    io_uring_sqe_set_data64(sync_sqe, 2);
    int submitted;
    do { submitted = io_uring_submit(&e->ring); } while (submitted == -EINTR);
    if (submitted != 2) return submitted < 0 ? -submitted : EIO;
    int timing_error = now_ns(CLOCK_MONOTONIC, admitted);
    int values[2] = { 0, 0 };
    bool seen[2] = { false, false };
    for (unsigned i = 0; i < 2; ++i) {
        struct io_uring_cqe *cqe;
        int error;
        do { error = io_uring_wait_cqe(&e->ring, &cqe); } while (error == -EINTR);
        if (error < 0) return -error;
        uint64_t id = io_uring_cqe_get_data64(cqe);
        if (id < 1 || id > 2 || seen[id - 1]) {
            io_uring_cqe_seen(&e->ring, cqe);
            return EIO;
        }
        values[id - 1] = cqe->res;
        seen[id - 1] = true;
        io_uring_cqe_seen(&e->ring, cqe);
        if (id == 1) {
            error = now_ns(CLOCK_MONOTONIC, write_complete);
            if (!timing_error) timing_error = error;
        }
    }
    int written = values[0], flushed = values[1];
    if (written < 0 && written != -EINTR) return -written;
    if (written == 0 || (written > 0 && (size_t)written > r->size)) return EIO;
    if (flushed < 0 && flushed != -EINTR && flushed != -ECANCELED) return -flushed;
    if (written == -EINTR || (size_t)written < r->size || flushed == -EINTR) {
        size_t done = written > 0 && (size_t)written < r->size ? (size_t)written : 0;
        struct request remainder = { r->data + done, r->size - done, r->offset + (off_t)done };
        uint64_t ignored_admission;
        int error = ring_transaction(e, &remainder, &ignored_admission, write_complete);
        return error ? error : timing_error;
    }
    if (flushed != 0) return flushed < 0 ? -flushed : EIO;
    return timing_error;
}

static int transaction(struct engine *e, const struct request *r,
                       uint64_t *submit, uint64_t *written, uint64_t *durable)
{
    uint64_t start, admitted = 0, write_complete = 0, end;
    int error = now_ns(CLOCK_MONOTONIC, &start);
    if (error) return error;
    if (e->kind != ENGINE_WORKER && e->kind != ENGINE_WORKER_RING) {
        error = reserve_request(e, r);
        if (error) return error;
    }
    if (e->kind == ENGINE_SYNC) {
        error = write_sync(e->fd, r, &write_complete);
    } else if (e->kind == ENGINE_WORKER) {
        thread_check(pthread_mutex_lock(&e->mutex));
        e->request = *r;
        e->done = false;
        e->pending = true;
        thread_check(pthread_cond_signal(&e->request_cv));
        thread_check(pthread_mutex_unlock(&e->mutex));
        int timing_error = now_ns(CLOCK_MONOTONIC, &admitted);
        thread_check(pthread_mutex_lock(&e->mutex));
        while (!e->done) thread_check(pthread_cond_wait(&e->done_cv, &e->mutex));
        error = e->worker_error;
        write_complete = e->worker_write_complete;
        thread_check(pthread_mutex_unlock(&e->mutex));
        if (!error) error = timing_error;
    } else if (e->kind == ENGINE_WORKER_RING) {
        struct completion c = { 0 };
        worker_ring_submit(e, r, &c);
        int timing_error = now_ns(CLOCK_MONOTONIC, &admitted);
        error = worker_ring_wait(e, &c, &write_complete);
        if (!error) error = timing_error;
    } else if (e->kind == ENGINE_AIO) {
        error = aio_transaction(e->fd, r, &admitted, &write_complete);
    } else if (e->kind == ENGINE_URING_LINKED) {
        error = ring_linked_transaction(e, r, &admitted, &write_complete);
    } else {
        error = ring_transaction(e, r, &admitted, &write_complete);
    }
    if (error) return error;
    if ((error = now_ns(CLOCK_MONOTONIC, &end))) return error;
    if (e->kind == ENGINE_SYNC) admitted = end;
    *submit = admitted - start;
    *written = write_complete - start;
    *durable = end - start;
    return 0;
}

static int pace(uint64_t us)
{
    struct timespec t = { .tv_sec = (time_t)(us / 1000000),
                          .tv_nsec = (long)((us % 1000000) * 1000) };
    while (nanosleep(&t, &t) != 0) { if (errno != EINTR) return errno; }
    return 0;
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static struct stats summarize(uint64_t *samples, size_t n)
{
    long double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += samples[i];
    qsort(samples, n, sizeof(*samples), compare_u64);
    return (struct stats) { samples[(n * 50 + 99) / 100 - 1],
        samples[(n * 95 + 99) / 100 - 1], samples[(n * 99 + 99) / 100 - 1],
        samples[n - 1], sum / n };
}

static void print_stats(const char *key, struct stats s)
{
    printf("\"%s\":{\"p50\":%" PRIu64 ",\"p95\":%" PRIu64
           ",\"p99\":%" PRIu64 ",\"max\":%" PRIu64 ",\"mean\":%.3Lf}",
           key, s.p50, s.p95, s.p99, s.max, s.mean);
}

static void json_string(const char *s)
{
    putchar('"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 32 || c >= 127) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static void print_allocation(const struct allocation *a)
{
    printf("\"allocation\":{\"chunk_bytes\":%" PRIu64
           ",\"allocation_calls\":%" PRIu64 ",\"growth_calls\":%" PRIu64
           ",\"initial_allocation_ns\":%" PRIu64 ",\"allocation_ns_total\":%" PRIu64
           ",\"allocation_ns_max\":%" PRIu64 ",\"reserved_bytes\":%" PRIu64
           ",\"initial_size_before\":%" PRId64 ",\"initial_size_after\":%" PRId64
           ",\"initial_blocks_before\":%" PRId64 ",\"initial_blocks_after\":%" PRId64 "}",
           a->chunk_bytes, a->allocation_calls, a->growth_calls, a->initial_allocation_ns,
           a->allocation_ns_total, a->allocation_ns_max, a->reserved_bytes,
           a->initial_size_before, a->initial_size_after,
           a->initial_blocks_before, a->initial_blocks_after);
}

static void print_limits(void)
{
    printf("\"limits\":{\"min_record_bytes\":8,\"max_batch_bytes\":%" PRIu64
           ",\"max_file_bytes_including_warmup\":%" PRIu64
           ",\"max_iterations\":%" PRIu64 ",\"max_pace_us\":%" PRIu64
           ",\"max_requested_pacing_us_including_warmup\":%" PRIu64
           ",\"max_explicit_buffer_bytes\":%" PRIu64 "}",
           MAX_BATCH_BYTES, MAX_FILE_BYTES, MAX_ITERATIONS, MAX_PACE_US,
           MAX_PACED_US, 2 * MAX_BATCH_BYTES + 3 * MAX_ITERATIONS * sizeof(uint64_t));
}

static bool unsupported_error(int error, const char *stage, enum engine_kind kind)
{
    if (error == ENOSYS || error == EOPNOTSUPP) return true;
    if (!strcmp(stage, "preallocation") && error == EINVAL) return true;
    if (!strcmp(stage, "engine_setup") &&
        (kind == ENGINE_URING || kind == ENGINE_URING_LINKED) &&
        (error == EPERM || error == EACCES || error == EINVAL)) return true;
    return false;
}

static void probe_wait_after(struct queue_probe *p, _Atomic uint32_t *counter,
                             uint32_t previous)
{
    for (;;) {
        uint32_t seq = atomic_load_explicit(&p->event, memory_order_acquire);
        if (atomic_load_explicit(counter, memory_order_acquire) > previous) return;
        event_wait(&p->event, seq);
    }
}

static void *release_full_queue(void *arg)
{
    struct queue_probe *p = arg;
    probe_wait_after(p, &p->full, 0);
    event_signal(&p->gate);
    return NULL;
}

static void *release_stopped_queue(void *arg)
{
    struct engine *e = arg;
    for (;;) {
        uint32_t seq = atomic_load_explicit(&e->request_event, memory_order_acquire);
        if (atomic_load_explicit(&e->stopping, memory_order_acquire)) break;
        event_wait(&e->request_event, seq);
    }
    event_signal(&e->probe->gate);
    return NULL;
}

static void queue_check_timeout(int signal_number)
{
    (void)signal_number;
    static const char message[] =
        "{\"schema\":\"saving.queue_check.v1\",\"verified\":false,"
        "\"checks\":[{\"name\":\"watchdog\",\"pass\":false}],"
        "\"error\":\"queue check exceeded 120 seconds\"}\n";
    ssize_t ignored = write(STDOUT_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    _exit(1);
}

/* Not a timing experiment. The worker starts behind a gate; the releaser only
 * opens it once the producer reaches the full-queue wait path. Every buffer
 * and completion has distinct stable storage until that request is reaped.
 * After reaping a wave its buffers are poisoned, then reused by the next wave.
 * The 120 s process-local watchdog turns a lost wake into a finite failure. */
static int queue_check(const char *directory)
{
    enum { WRAP_REQUESTS = 2048, WAKE_REQUESTS = 32, RECORD_BYTES = 32 };
    enum { CHECK_COUNT = 8 };
    const char *names[CHECK_COUNT] = {
        "forced_full_backpressure", "many_wraps", "durable_completions",
        "immutable_distinct_buffers_exact_offsets", "empty_sleep_wake",
        "idle_shutdown", "outstanding_shutdown_drain", "real_write_error"
    };
    bool checks[CHECK_COUNT] = { false };
    struct queue_probe probe = { 0 }, drain_probe = { 0 };
    struct engine e = { .kind = ENGINE_WORKER_RING, .fd = -1, .probe = &probe };
    struct engine drain = { .kind = ENGINE_WORKER_RING, .fd = -1, .probe = &drain_probe };
    struct engine failing = { .kind = ENGINE_WORKER_RING, .fd = -1 };
    unsigned char buffers[WORKER_RING_SLOTS + 1][RECORD_BYTES];
    struct completion completions[WORKER_RING_SLOTS + 1] = { 0 };
    struct completion failure_completion = { 0 };
    char path[PATH_MAX];
    bool own_file = false, releaser_ready = false, drain_releaser_ready = false;
    pthread_t releaser, drain_releaser;
    int error = 0, fd = -1, full_fd = -1, write_error = 0;
    uint64_t submitted = 0, reaped = 0, verified_records = 0;
    uint32_t wake_cycles = 0;
    struct sigaction action = { .sa_handler = queue_check_timeout };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, NULL) != 0) { error = errno; goto cleanup; }
    alarm(120);
    int path_length = snprintf(path, sizeof(path), "%s/io-queue-check-XXXXXX", directory);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
        error = ENAMETOOLONG;
        goto cleanup;
    }
    fd = mkostemp(path, O_CLOEXEC);
    if (fd < 0) { error = errno; goto cleanup; }
    own_file = true;
    /* Open-but-unlinked means even watchdog termination leaves no test file. */
    if (unlink(path) != 0) { error = errno; goto cleanup; }
    own_file = false;
    e.fd = drain.fd = fd;
    if ((error = engine_init(&e))) goto cleanup;
    for (unsigned i = 0; i <= WORKER_RING_SLOTS; ++i)
        fill_batch(buffers[i], RECORD_BYTES, 1, i);
    for (unsigned i = 0; i < WORKER_RING_SLOTS; ++i) {
        struct request r = { buffers[i], RECORD_BYTES, (off_t)i * RECORD_BYTES };
        if (!worker_ring_try_submit(&e, &r, &completions[i])) {
            error = EIO;
            goto cleanup;
        }
        ++submitted;
    }
    struct request overflow = { buffers[WORKER_RING_SLOTS], RECORD_BYTES,
                               (off_t)WORKER_RING_SLOTS * RECORD_BYTES };
    if (worker_ring_try_submit(&e, &overflow, &completions[WORKER_RING_SLOTS])) {
        ++submitted;
        error = EOVERFLOW;
        goto cleanup;
    }
    error = pthread_create(&releaser, NULL, release_full_queue, &probe);
    if (error) goto cleanup;
    releaser_ready = true;
    worker_ring_submit(&e, &overflow, &completions[WORKER_RING_SLOTS]);
    ++submitted;
    thread_check(pthread_join(releaser, NULL));
    releaser_ready = false;
    checks[0] = atomic_load_explicit(&probe.full, memory_order_acquire) > 0;
    for (unsigned i = 0; i <= WORKER_RING_SLOTS; ++i) {
        uint64_t written;
        if ((error = worker_ring_wait(&e, &completions[i], &written))) goto cleanup;
        ++reaped;
    }
    while (submitted < WRAP_REQUESTS) {
        unsigned count = WRAP_REQUESTS - submitted < WORKER_RING_SLOTS ?
                         (unsigned)(WRAP_REQUESTS - submitted) : WORKER_RING_SLOTS;
        memset(buffers, 0xa5, sizeof(buffers));
        for (unsigned i = 0; i < count; ++i) {
            fill_batch(buffers[i], RECORD_BYTES, 1, submitted);
            struct request r = { buffers[i], RECORD_BYTES, (off_t)submitted * RECORD_BYTES };
            worker_ring_submit(&e, &r, &completions[i]);
            ++submitted;
        }
        /* Reap in reverse order: all completion records must survive later
         * requests and ring-slot reuse, not merely report the last request. */
        for (unsigned i = count; i > 0; --i) {
            uint64_t written;
            if ((error = worker_ring_wait(&e, &completions[i - 1], &written))) goto cleanup;
            ++reaped;
        }
    }
    checks[1] = submitted / WORKER_RING_SLOTS >= 32;
    for (unsigned i = 0; i < WAKE_REQUESTS; ++i) {
        uint32_t idle = atomic_load_explicit(&probe.idle, memory_order_acquire);
        /* Ask for a fresh empty wait, even if the worker is already asleep.
         * The handshake covers both actual sleeps and enqueue-vs-sleep races;
         * scheduling delays are never used as correctness assertions. */
        event_signal(&e.request_event);
        probe_wait_after(&probe, &probe.idle, idle);
        fill_batch(buffers[0], RECORD_BYTES, 1, submitted);
        struct request r = { buffers[0], RECORD_BYTES, (off_t)submitted * RECORD_BYTES };
        worker_ring_submit(&e, &r, &completions[0]);
        ++submitted;
        uint64_t written;
        if ((error = worker_ring_wait(&e, &completions[0], &written))) goto cleanup;
        ++reaped;
        ++wake_cycles;
    }
    checks[4] = wake_cycles == WAKE_REQUESTS;
    uint32_t idle = atomic_load_explicit(&probe.idle, memory_order_acquire);
    event_signal(&e.request_event);
    probe_wait_after(&probe, &probe.idle, idle);
    engine_destroy(&e);
    checks[5] = atomic_load_explicit(&e.completed, memory_order_acquire) == submitted;

    if ((error = engine_init(&drain))) goto cleanup;
    for (unsigned i = 0; i < WORKER_RING_SLOTS; ++i) {
        fill_batch(buffers[i], RECORD_BYTES, 1, submitted);
        struct request r = { buffers[i], RECORD_BYTES, (off_t)submitted * RECORD_BYTES };
        worker_ring_submit(&drain, &r, &completions[i]);
        ++submitted;
    }
    /* Hold all 64 requests until destroy publishes stop: shutdown must drain
     * real outstanding work, independent of worker scheduling or disk speed. */
    error = pthread_create(&drain_releaser, NULL, release_stopped_queue, &drain);
    if (error) goto cleanup;
    drain_releaser_ready = true;
    engine_destroy(&drain);
    thread_check(pthread_join(drain_releaser, NULL));
    drain_releaser_ready = false;
    for (unsigned i = 0; i < WORKER_RING_SLOTS; ++i) {
        if (!atomic_load_explicit(&completions[i].done, memory_order_acquire)) {
            error = EIO;
            goto cleanup;
        }
        if ((error = completions[i].error)) goto cleanup;
        ++reaped;
    }
    checks[6] = atomic_load_explicit(&drain.completed, memory_order_acquire) == WORKER_RING_SLOTS;
    checks[2] = reaped == submitted;
    /* Each successful completion follows checked fdatasync, not merely write.
     * Read via the actual file descriptor, checking exact EOF and every byte. */
    struct stat st;
    if (fstat(fd, &st) != 0) { error = errno; goto cleanup; }
    if (st.st_size != (off_t)submitted * RECORD_BYTES) { error = EIO; goto cleanup; }
    for (uint64_t i = 0; i < submitted; ++i) {
        unsigned char actual[RECORD_BYTES], expected[RECORD_BYTES];
        size_t done = 0;
        while (done < RECORD_BYTES) {
            ssize_t n = pread(fd, actual + done, RECORD_BYTES - done,
                              (off_t)i * RECORD_BYTES + (off_t)done);
            if (n < 0) { if (errno == EINTR) continue; error = errno; goto cleanup; }
            if (!n) { error = EIO; goto cleanup; }
            done += (size_t)n;
        }
        fill_batch(expected, RECORD_BYTES, 1, i);
        if (memcmp(actual, expected, RECORD_BYTES)) { error = EILSEQ; goto cleanup; }
        ++verified_records;
    }
    checks[3] = verified_records == submitted;

    full_fd = open("/dev/full", O_WRONLY | O_CLOEXEC);
    if (full_fd < 0) { error = errno; goto cleanup; }
    failing.fd = full_fd;
    if ((error = engine_init(&failing))) goto cleanup;
    struct request fail_request = { buffers[0], RECORD_BYTES, 0 };
    worker_ring_submit(&failing, &fail_request, &failure_completion);
    uint64_t failed_write_timestamp;
    write_error = worker_ring_wait(&failing, &failure_completion, &failed_write_timestamp);
    checks[7] = write_error == ENOSPC;
    if (!checks[7]) error = write_error ? write_error : EIO;
cleanup:
    /* Unblock check-only gates before joining even on setup/check failure. */
    if (!atomic_load_explicit(&probe.gate, memory_order_acquire)) event_signal(&probe.gate);
    if (!atomic_load_explicit(&drain_probe.gate, memory_order_acquire))
        event_signal(&drain_probe.gate);
    if (releaser_ready) {
        atomic_fetch_add_explicit(&probe.full, 1, memory_order_release);
        event_signal(&probe.event);
        thread_check(pthread_join(releaser, NULL));
    }
    engine_destroy(&e);
    engine_destroy(&drain);
    if (drain_releaser_ready) thread_check(pthread_join(drain_releaser, NULL));
    engine_destroy(&failing);
    if (fd >= 0 && close(fd) != 0 && !error) error = errno;
    if (full_fd >= 0 && close(full_fd) != 0 && !error) error = errno;
    if (own_file && unlink(path) != 0 && !error) error = errno;
    alarm(0);
    bool verified = !error;
    for (unsigned i = 0; i < CHECK_COUNT; ++i) verified = verified && checks[i];
    printf("{\"schema\":\"saving.queue_check.v1\",\"verified\":%s,\"checks\":[",
           verified ? "true" : "false");
    for (unsigned i = 0; i < CHECK_COUNT; ++i) {
        if (i) putchar(',');
        printf("{\"name\":\"%s\",\"pass\":%s}", names[i], checks[i] ? "true" : "false");
    }
    printf("],\"ring_slots\":%u,\"submitted_requests\":%" PRIu64
           ",\"successful_completions\":%" PRIu64 ",\"verified_records\":%" PRIu64
           ",\"wake_cycles\":%" PRIu32 ",\"full_wait_entries\":%" PRIu32
           ",\"error_requests\":%u,\"write_errno\":%d,\"errno\":%d}\n",
           WORKER_RING_SLOTS, submitted, reaped, verified_records, wake_cycles,
           atomic_load_explicit(&probe.full, memory_order_acquire),
           atomic_load_explicit(&failure_completion.done, memory_order_acquire) ? 1U : 0U,
           write_error, error);
    if (fflush(stdout) != 0 || ferror(stdout)) return 1;
    return verified ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[2], "queue-check")) return queue_check(argv[1]);
    uint64_t record_bytes = 0, records = 0, iterations = 0, pace_us = 0;
    enum engine_kind kind = ENGINE_SYNC;
    bool valid = argc == 7 || argc == 8;
    if (valid) {
        if (!strcmp(argv[2], "sync")) kind = ENGINE_SYNC;
        else if (!strcmp(argv[2], "worker")) kind = ENGINE_WORKER;
        else if (!strcmp(argv[2], "worker-ring")) kind = ENGINE_WORKER_RING;
        else if (!strcmp(argv[2], "aio")) kind = ENGINE_AIO;
        else if (!strcmp(argv[2], "uring")) kind = ENGINE_URING;
        else if (!strcmp(argv[2], "uring-linked")) kind = ENGINE_URING_LINKED;
        else valid = false;
        valid = valid && !parse_number(argv[3], &record_bytes) &&
            !parse_number(argv[4], &records) && !parse_number(argv[5], &iterations) &&
            (!strcmp(argv[6], "off") || !strcmp(argv[6], "keep-size") ||
             !strcmp(argv[6], "chunked-64k") || !strcmp(argv[6], "chunked-1m")) &&
            (argc == 7 || !parse_number(argv[7], &pace_us));
        valid = valid && record_bytes >= 8 && record_bytes <= MAX_BATCH_BYTES &&
            records > 0 && records <= MAX_BATCH_BYTES / record_bytes &&
            iterations > 0 && iterations <= MAX_ITERATIONS && pace_us <= MAX_PACE_US;
        if (valid) valid = iterations + WARMUP <= MAX_FILE_BYTES / (record_bytes * records) &&
            (pace_us == 0 || iterations + WARMUP <= MAX_PACED_US / pace_us);
    }
    if (!valid) {
        printf("{\"schema\":\"saving.io_bench.v1\",\"error\":\"invalid arguments\",\"errno\":%d,", EINVAL);
        print_limits();
        puts("}");
        fprintf(stderr, "Usage: %s DIR sync|worker|worker-ring|aio|uring|uring-linked RECORD_BYTES RECORDS_PER_SYNC ITERATIONS off|keep-size|chunked-64k|chunked-1m [PACE_US]\n       %s DIR queue-check\n", argv[0], argv[0]);
        return 2;
    }

    size_t batch_bytes = (size_t)(record_bytes * records);
    uint64_t transactions = iterations + WARMUP;
    uint64_t file_bytes = transactions * batch_bytes;
    uint64_t setup_start = 0, setup_end = 0;
    uint64_t cpu_start = 0, cpu_end = 0, wall_start = 0, wall_end = 0;
    uint64_t *submit = NULL, *written = NULL, *durable = NULL;
    unsigned char *data = NULL, *actual = NULL;
    char *path = NULL;
    int fd = -1, error = 0;
    const char *stage = "setup_clock";
    bool own_file = false, verified = false;
    struct engine engine = { .kind = kind, .fd = -1 };
    if ((error = now_ns(CLOCK_MONOTONIC, &setup_start))) goto cleanup;
    stage = "allocation";
    data = malloc(batch_bytes);
    actual = malloc(batch_bytes);
    submit = calloc((size_t)iterations, sizeof(*submit));
    written = calloc((size_t)iterations, sizeof(*written));
    durable = calloc((size_t)iterations, sizeof(*durable));
    size_t dir_len = strlen(argv[1]);
    if (dir_len > PATH_MAX - 64) { error = ENAMETOOLONG; goto cleanup; }
    path = malloc(dir_len + 64);
    if (!data || !actual || !submit || !written || !durable || !path) { error = ENOMEM; goto cleanup; }
    snprintf(path, dir_len + 64, "%s/io-bench-XXXXXX", argv[1]);
    stage = "file_creation";
    fd = mkostemp(path, O_CLOEXEC);
    if (fd < 0) { error = errno; goto cleanup; }
    own_file = true;
    engine.fd = fd;
    stage = "preallocation";
    if ((error = reserve_initial(&engine, argv[6], file_bytes))) goto cleanup;
    stage = "engine_setup";
    if ((error = engine_init(&engine))) goto cleanup;
    if ((error = now_ns(CLOCK_MONOTONIC, &setup_end))) goto cleanup;
    stage = "warmup";
    for (uint64_t i = 0; i < WARMUP; ++i) {
        fill_batch(data, (size_t)record_bytes, (size_t)records, i * records);
        if (pace_us && (error = pace(pace_us))) goto cleanup;
        struct request r = { data, batch_bytes, (off_t)(i * batch_bytes) };
        uint64_t ignored_submit, ignored_written, ignored_durable;
        if ((error = transaction(&engine, &r, &ignored_submit,
                                  &ignored_written, &ignored_durable))) goto cleanup;
    }
    stage = "measured_transactions";
    if ((error = now_ns(CLOCK_MONOTONIC, &wall_start))) goto cleanup;
    if ((error = now_ns(CLOCK_PROCESS_CPUTIME_ID, &cpu_start))) goto cleanup;
    for (uint64_t i = 0; i < iterations; ++i) {
        uint64_t index = i + WARMUP;
        fill_batch(data, (size_t)record_bytes, (size_t)records, index * records);
        if (pace_us && (error = pace(pace_us))) goto cleanup;
        struct request r = { data, batch_bytes, (off_t)(index * batch_bytes) };
        if ((error = transaction(&engine, &r, &submit[i], &written[i], &durable[i]))) goto cleanup;
    }
    if ((error = now_ns(CLOCK_PROCESS_CPUTIME_ID, &cpu_end))) goto cleanup;
    if ((error = now_ns(CLOCK_MONOTONIC, &wall_end))) goto cleanup;
    stage = "verification";
    struct stat st;
    if (fstat(fd, &st) != 0) { error = errno; goto cleanup; }
    if (st.st_size != (off_t)file_bytes) { error = EIO; goto cleanup; }
    for (uint64_t i = 0; i < transactions; ++i) {
        fill_batch(data, (size_t)record_bytes, (size_t)records, i * records);
        size_t done = 0;
        while (done < batch_bytes) {
            ssize_t n = pread(fd, actual + done, batch_bytes - done,
                              (off_t)(i * batch_bytes + done));
            if (n < 0) { if (errno == EINTR) continue; error = errno; goto cleanup; }
            if (!n) { error = EIO; goto cleanup; }
            done += (size_t)n;
        }
        if (memcmp(data, actual, batch_bytes)) { error = EILSEQ; goto cleanup; }
        /* Explicitly check unique record numbers as well as every byte/offset. */
        for (uint64_t r = 0; r < records; ++r) {
            uint64_t id = 0;
            for (unsigned k = 0; k < 8; ++k)
                id |= (uint64_t)actual[r * record_bytes + k] << (8 * k);
            if (id != i * records + r) { error = EILSEQ; goto cleanup; }
        }
    }
    verified = true;
cleanup:
    engine_destroy(&engine);
    if (engine.allocation.failed) stage = "preallocation";
    if (fd >= 0 && close(fd) != 0 && !error) { error = errno; stage = "close"; }
    if (own_file && unlink(path) != 0) {
        int unlink_error = errno;
        fprintf(stderr, "Could not remove own generated file %s: %s\n", path, strerror(unlink_error));
        if (!error) { error = unlink_error; stage = "unlink"; }
    }
    if (!setup_end && setup_start) {
        uint64_t end;
        if (!now_ns(CLOCK_MONOTONIC, &end)) setup_end = end;
    }
    printf("{\"schema\":\"saving.io_bench.v1\",\"engine\":");
    json_string(argv[2]);
    printf(",\"record_bytes\":%" PRIu64 ",\"records_per_sync\":%" PRIu64
           ",\"iterations\":%" PRIu64 ",\"pace_us\":%" PRIu64 ",\"preallocation\":",
           record_bytes, records, iterations, pace_us);
    json_string(argv[6]);
    printf(",\"setup_ns\":%" PRIu64 ",\"prealloc_ns\":%" PRIu64 ",",
           setup_end >= setup_start ? setup_end - setup_start : 0,
           engine.allocation.initial_allocation_ns);
    print_limits();
    putchar(','); print_allocation(&engine.allocation);
    bool unsupported = error && unsupported_error(error, stage, kind);
    if (error) {
        printf(",\"unsupported\":%s,\"errno\":%d,\"stage\":", unsupported ? "true" : "false", error);
        json_string(stage);
        printf(",\"error\":");
        json_string(strerror(error));
        printf(",\"verified\":%s}\n", verified ? "true" : "false");
    } else {
        putchar(','); print_stats("submit_ns", summarize(submit, (size_t)iterations));
        putchar(','); print_stats("write_complete_ns", summarize(written, (size_t)iterations));
        putchar(','); print_stats("durable_ns", summarize(durable, (size_t)iterations));
        printf(",\"cpu_ns\":%" PRIu64 ",\"wall_ns\":%" PRIu64
               ",\"bytes\":%" PRIu64 ",\"flushes\":%" PRIu64
               ",\"warmup_transactions\":%u,\"file_bytes_including_warmup\":%" PRIu64
               ",\"verified_records_including_warmup\":%" PRIu64
               ",\"latency_unit\":\"durable_group\",\"cpu_wall_scope\":\"measured_loop_including_generation_and_pacing\""
               ",\"percentiles\":\"nearest_rank\",\"verified\":true}\n",
               cpu_end - cpu_start, wall_end - wall_start, iterations * batch_bytes,
               iterations, WARMUP, file_bytes, transactions * records);
    }
    free(path); free(data); free(actual); free(submit); free(written); free(durable);
    if (fflush(stdout) != 0 || ferror(stdout)) return 1;
    return error ? (unsupported ? 77 : 1) : 0;
}
