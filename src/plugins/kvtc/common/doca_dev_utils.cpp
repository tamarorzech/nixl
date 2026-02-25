#include "doca_dev_utils.h"
#include "comch_error.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>

namespace {
doca_error_t
find_doca_device_by_bdf(const std::string &bdf,
                        struct doca_devinfo **dev_list,
                        uint32_t num_devs,
                        struct doca_devinfo **match_out) {
    char addr[DOCA_DEVINFO_PCI_ADDR_SIZE] = {0};
    for (uint32_t i = 0; i < num_devs; i++) {
        doca_error_t rc = doca_devinfo_get_pci_addr_str(dev_list[i], addr);
        if (rc == DOCA_SUCCESS && strcmp(addr, bdf.c_str()) == 0) {
            *match_out = dev_list[i];
            return rc;
        }
    }
    return DOCA_ERROR_NOT_FOUND;
}

void
default_mmap_free_cb(void *addr, size_t len, void *opaque) {
    (void)len;
    (void)opaque;
    free(addr);
}
} // namespace

void
doca_dev_utils::wait_for_ctx_running(struct doca_ctx *ctx,
                                     struct doca_pe *pe,
                                     std::chrono::milliseconds timeout,
                                     const std::string &ctx_name) {
    const auto start_time = std::chrono::steady_clock::now();
    enum doca_ctx_states state;

    do {
        (void)doca_pe_progress(pe);

        doca_error_t rc = doca_ctx_get_state(ctx, &state);
        if (rc != DOCA_SUCCESS)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "doca_ctx_get_state failed: " + std::string(doca_error_get_descr(rc)));

        switch (state) {
        case DOCA_CTX_STATE_RUNNING:
            return;
        case DOCA_CTX_STATE_STARTING:
            break;
        default:
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             ctx_name + " is in invalid state: " + std::to_string(state));
        }

        const auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout) {
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "Timeout waiting for " + ctx_name + " to start");
        }
        std::this_thread::sleep_for(kCtxStartPollInterval);
    } while (true);
}

nixl_doca_dev_ptr
doca_dev_utils::open_doca_device_by_bdf(const std::string &bdf) {
    if (bdf.empty()) throw ComchError(ComchErrorCode::COMCH_INVALID_ARG, "bdf is empty");

    struct doca_devinfo **dev_list_raw;
    uint32_t num_devs;
    doca_error_t rc = doca_devinfo_create_list(&dev_list_raw, &num_devs);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_devinfo_create_list failed: " +
                             std::string(doca_error_get_descr(rc)));
    nixl_doca_devinfo_list_ptr dev_list(dev_list_raw);

    struct doca_devinfo *match;
    rc = find_doca_device_by_bdf(bdf, dev_list_raw, num_devs, &match);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "find_doca_device_by_bdf failed: " +
                             std::string(doca_error_get_descr(rc)));

    struct doca_dev *dev_ptr_raw;
    rc = doca_dev_open(match, &dev_ptr_raw);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_dev_open failed: " + std::string(doca_error_get_descr(rc)));
    nixl_doca_dev_ptr dev_ptr(dev_ptr_raw);

    if (!dev_ptr.get())
        throw ComchError(ComchErrorCode::COMCH_NOT_FOUND,
                         "open_doca_device_by_bdf failed: device not found");

    return dev_ptr;
}

nixl_doca_dev_rep_ptr
doca_dev_utils::open_doca_dev_rep_by_bdf(const std::string &bdf) {
    if (bdf.empty()) throw ComchError(ComchErrorCode::COMCH_INVALID_ARG, "bdf is empty");

    struct doca_devinfo **dev_list_raw;
    uint32_t num_devs;
    doca_error_t rc = doca_devinfo_create_list(&dev_list_raw, &num_devs);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_devinfo_create_list failed: " +
                             std::string(doca_error_get_descr(rc)));
    nixl_doca_devinfo_list_ptr dev_list(dev_list_raw);

    struct doca_devinfo *match;
    rc = find_doca_device_by_bdf(bdf, dev_list_raw, num_devs, &match);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "find_doca_device_by_bdf failed: " +
                             std::string(doca_error_get_descr(rc)));

    struct doca_dev_rep *rep_ptr_raw;
    rc = doca_dev_rep_open((struct doca_devinfo_rep *)match, &rep_ptr_raw);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_dev_rep_open failed: " + std::string(doca_error_get_descr(rc)));
    nixl_doca_dev_rep_ptr rep_ptr(rep_ptr_raw);

    if (!rep_ptr.get())
        throw ComchError(ComchErrorCode::COMCH_NOT_FOUND,
                         "open_doca_dev_rep_by_bdf failed: device not found");

    return rep_ptr;
}

nixl_doca_pe_ptr
doca_dev_utils::create_doca_pe() {
    struct doca_pe *pe_ptr_raw;
    doca_error_t rc = doca_pe_create(&pe_ptr_raw);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_pe_create failed: " + std::string(doca_error_get_descr(rc)));
    nixl_doca_pe_ptr pe_ptr(pe_ptr_raw);

    return pe_ptr;
}

std::string
doca_dev_utils::doca_ctx_states_to_string(enum doca_ctx_states state) {
    switch (state) {
    case DOCA_CTX_STATE_IDLE:
        return "DOCA_CTX_STATE_IDLE";
    case DOCA_CTX_STATE_STARTING:
        return "DOCA_CTX_STATE_STARTING";
    case DOCA_CTX_STATE_RUNNING:
        return "DOCA_CTX_STATE_RUNNING";
    case DOCA_CTX_STATE_STOPPING:
        return "DOCA_CTX_STATE_STOPPING";
    default:
        return "UNKNOWN_DOCA_CTX_STATE: " + std::to_string(static_cast<int>(state));
    }
}

nixl_doca_mmap_ptr
doca_dev_utils::create_configured_mmap(void *buf,
                                       size_t size,
                                       uint32_t access_flags,
                                       struct doca_dev *dev) {
    struct doca_mmap *mmap = nullptr;
    doca_error_t rc = doca_mmap_create(&mmap);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_create failed: " + std::string(doca_error_get_descr(rc)));

    nixl_doca_mmap_ptr mmap_ptr(mmap);

    rc = doca_mmap_set_memrange(mmap_ptr.get(), buf, size);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_set_memrange failed: " + std::string(doca_error_get_descr(rc)));

    rc = doca_mmap_set_permissions(mmap_ptr.get(), access_flags);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_set_permissions failed: " +
                             std::string(doca_error_get_descr(rc)));

    // add_dev must be called before start
    rc = doca_mmap_add_dev(mmap_ptr.get(), dev);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_add_dev failed: " + std::string(doca_error_get_descr(rc)));

    rc = doca_mmap_start(mmap_ptr.get());
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_start failed: " + std::string(doca_error_get_descr(rc)));

    return mmap_ptr;
}

struct doca_comch_producer *
doca_dev_utils::create_producer(struct doca_comch_connection *conn,
                                struct doca_pe *pe,
                                ProducerSendCompletionCb on_complete,
                                ProducerSendErrorCb on_error,
                                uint32_t num_tasks,
                                nixl_doca_ctx &out_producer_ctx) {
    struct doca_comch_producer *producer = nullptr;
    doca_error_t rc = doca_comch_producer_create(conn, &producer);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_producer_create failed: " +
                             std::string(doca_error_get_descr(rc)));

    struct doca_ctx *pctx = doca_comch_producer_as_ctx(producer);
    if (pctx == nullptr) {
        doca_comch_producer_destroy(producer);
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR, "doca_comch_producer_as_ctx failed");
    }
    out_producer_ctx.reset(pctx);

    rc = doca_pe_connect_ctx(pe, pctx);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_pe_connect_ctx(producer) failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_comch_producer_task_send_set_conf(producer, on_complete, on_error, num_tasks);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_producer_task_send_set_conf failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_ctx_start(pctx);
    if (rc != DOCA_SUCCESS && rc != DOCA_ERROR_IN_PROGRESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_ctx_start(producer) failed: " +
                             std::string(doca_error_get_descr(rc)));

    return producer;
}

struct doca_comch_consumer *
doca_dev_utils::create_consumer(struct doca_comch_connection *conn,
                                struct doca_pe *pe,
                                struct doca_mmap *mmap,
                                ConsumerPostRecvCompletionCb on_complete,
                                ConsumerPostRecvErrorCb on_error,
                                uint32_t num_tasks,
                                uint32_t imm_data_len,
                                nixl_doca_ctx &out_consumer_ctx) {
    struct doca_comch_consumer *consumer = nullptr;
    doca_error_t rc = doca_comch_consumer_create(conn, mmap, &consumer);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_consumer_create failed: " +
                             std::string(doca_error_get_descr(rc)));

    struct doca_ctx *cctx = doca_comch_consumer_as_ctx(consumer);
    if (cctx == nullptr) {
        doca_comch_consumer_destroy(consumer);
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR, "doca_comch_consumer_as_ctx failed");
    }
    out_consumer_ctx.reset(cctx);

    rc = doca_pe_connect_ctx(pe, cctx);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_pe_connect_ctx(consumer) failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_comch_consumer_task_post_recv_set_conf(consumer, on_complete, on_error, num_tasks);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_consumer_task_post_recv_set_conf failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_comch_consumer_set_imm_data_len(consumer, imm_data_len);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_consumer_set_imm_data_len failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_ctx_start(cctx);
    if (rc != DOCA_SUCCESS && rc != DOCA_ERROR_IN_PROGRESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_ctx_start(consumer) failed: " +
                             std::string(doca_error_get_descr(rc)));

    return consumer;
}

nixl_doca_mmap_ptr
doca_dev_utils::create_consumer_mmap(struct doca_dev *dev,
                                     size_t mmap_size,
                                     uint32_t access_flags,
                                     void **out_base) {
    struct doca_mmap *mmap_raw = nullptr;
    doca_error_t rc = doca_mmap_create(&mmap_raw);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_create failed: " + std::string(doca_error_get_descr(rc)));

    nixl_doca_mmap_ptr mmap_ptr(mmap_raw);

    void *mmap_base = nullptr;
    size_t page_size = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    if (posix_memalign(&mmap_base, page_size, mmap_size) != 0) {
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "posix_memalign failed: " + std::string(strerror(errno)));
    }

    rc = doca_mmap_set_memrange(mmap_ptr.get(), mmap_base, mmap_size);
    if (rc != DOCA_SUCCESS) {
        free(mmap_base);
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_set_memrange failed: " + std::string(doca_error_get_descr(rc)));
    }

    rc = doca_mmap_set_free_cb(mmap_ptr.get(), default_mmap_free_cb, nullptr);
    if (rc != DOCA_SUCCESS) {
        free(mmap_base);
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_set_free_cb failed: " + std::string(doca_error_get_descr(rc)));
    }

    rc = doca_mmap_set_permissions(mmap_ptr.get(), access_flags);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_set_permissions failed: " +
                             std::string(doca_error_get_descr(rc)));

    // add_dev must be called before start
    rc = doca_mmap_add_dev(mmap_ptr.get(), dev);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_add_dev failed: " + std::string(doca_error_get_descr(rc)));

    rc = doca_mmap_start(mmap_ptr.get());
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_mmap_start failed: " + std::string(doca_error_get_descr(rc)));

    *out_base = mmap_base;
    return mmap_ptr;
}

struct doca_buf_inventory *
doca_dev_utils::create_buf_inventory(uint32_t num_bufs) {
    struct doca_buf_inventory *buf_inventory = nullptr;
    doca_error_t rc = doca_buf_inventory_create(num_bufs, &buf_inventory);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_buf_inventory_create failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_buf_inventory_start(buf_inventory);
    if (rc != DOCA_SUCCESS) {
        doca_buf_inventory_destroy(buf_inventory);
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_buf_inventory_start failed: " +
                             std::string(doca_error_get_descr(rc)));
    }

    return buf_inventory;
}

void
doca_dev_utils::submit_post_recv_tasks(struct doca_comch_consumer *consumer,
                                       struct doca_buf_inventory *buf_inventory,
                                       struct doca_mmap *mmap,
                                       void *mmap_base,
                                       size_t mmap_size,
                                       uint32_t num_tasks) {
    if (mmap_size % num_tasks != 0)
        throw ComchError(ComchErrorCode::COMCH_INVALID_ARG,
                         "Consumer mmap size must be divisible by num_tasks");

    size_t buf_size = mmap_size / num_tasks;

    for (uint32_t i = 0; i < num_tasks; ++i) {
        struct doca_buf *buf;
        void *buf_addr = static_cast<char *>(mmap_base) + i * buf_size;

        doca_error_t rc =
            doca_buf_inventory_buf_get_by_addr(buf_inventory, mmap, buf_addr, buf_size, &buf);
        if (rc != DOCA_SUCCESS)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "doca_buf_inventory_buf_get_by_addr failed (task " +
                                 std::to_string(i) + "): " + std::string(doca_error_get_descr(rc)));

        if (buf == nullptr)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "doca_buf_inventory_buf_get_by_addr returned nullptr (task " +
                                 std::to_string(i) + ")");

        struct doca_comch_consumer_task_post_recv *task = nullptr;
        rc = doca_comch_consumer_task_post_recv_alloc_init(consumer, buf, &task);
        if (rc != DOCA_SUCCESS)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "doca_comch_consumer_task_post_recv_alloc_init failed (task " +
                                 std::to_string(i) + "): " + std::string(doca_error_get_descr(rc)));
        if (task == nullptr)
            throw ComchError(
                ComchErrorCode::COMCH_INTERNAL_ERROR,
                "doca_comch_consumer_task_post_recv_alloc_init returned null task (task " +
                    std::to_string(i) + ")");

        rc = doca_task_submit(doca_comch_consumer_task_post_recv_as_task(task));
        if (rc != DOCA_SUCCESS)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "doca_task_submit(post_recv) failed (task " + std::to_string(i) +
                                 "): " + std::string(doca_error_get_descr(rc)));
    }
}
