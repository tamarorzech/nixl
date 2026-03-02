#ifndef COMCH_COMMON_DOCA_DEV_UTILS_H
#define COMCH_COMMON_DOCA_DEV_UTILS_H

extern "C" {
#include <doca_log.h>
#include <doca_dev.h>
#include <doca_ctx.h>
#include <doca_pe.h>
#include <doca_error.h>
#include <doca_comch.h>
#include <doca_mmap.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_buf.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <doca_buf_inventory.h>
#pragma GCC diagnostic pop
}

#include <chrono>
#include <memory>
#include <string>

typedef uint32_t task_id_t;

struct DocaDevDeleter {
    void
    operator()(struct doca_dev *dev) const noexcept {
        if (dev != nullptr) {
            doca_dev_close(dev);
        }
    }
};

struct DocaRepDeleter {
    void
    operator()(struct doca_dev_rep *rep) const noexcept {
        if (rep != nullptr) {
            doca_dev_rep_close(rep);
        }
    }
};

struct DocaCtxDeleter {
    void
    operator()(struct doca_ctx *ctx) const noexcept {
        if (ctx != nullptr) {
            doca_ctx_stop(ctx);
        }
    }
};

struct DocaPeDeleter {
    void
    operator()(struct doca_pe *pe) const noexcept {
        if (pe != nullptr) {
            doca_pe_destroy(pe);
        }
    }
};

struct DocaComchServerDeleter {
    void
    operator()(struct doca_comch_server *comch_server) const noexcept {
        if (comch_server != nullptr) {
            doca_comch_server_destroy(comch_server);
        }
    }
};

struct DocaComchClientDeleter {
    void
    operator()(struct doca_comch_client *comch_client) const noexcept {
        if (comch_client != nullptr) {
            (void)doca_comch_client_destroy(comch_client);
        }
    }
};

struct DocaDevListDeleter {
    void
    operator()(struct doca_devinfo **dev_list) const noexcept {
        if (dev_list != nullptr) {
            doca_devinfo_destroy_list(dev_list);
        }
    }
};

struct DocaComchTaskSendDeleter {
    void
    operator()(struct doca_comch_task_send *task_send) const noexcept {
        if (task_send != nullptr) {
            doca_task_free(doca_comch_task_send_as_task(task_send));
        }
    }
};

struct DocaComchProducerTaskSendDeleter {
    void
    operator()(struct doca_comch_producer_task_send *task_send) const noexcept {
        if (task_send != nullptr) {
            doca_task_free(doca_comch_producer_task_send_as_task(task_send));
        }
    }
};

struct DocaComchProducerDeleter {
    void
    operator()(struct doca_comch_producer *producer) const noexcept {
        if (producer != nullptr) {
            doca_comch_producer_destroy(producer);
        }
    }
};

struct DocaComchConsumerDeleter {
    void
    operator()(struct doca_comch_consumer *consumer) const noexcept {
        if (consumer != nullptr) {
            doca_comch_consumer_destroy(consumer);
        }
    }
};

struct DocaMmapDeleter {
    void
    operator()(struct doca_mmap *mmap) const noexcept {
        if (mmap != nullptr) {
            doca_mmap_destroy(mmap);
        }
    }
};

// struct DocaBufInventoryDeleter {
// void operator()(struct doca_buf_inventory *buf_inventory) const noexcept
//     {
//         if (buf_inventory != nullptr) {
//             doca_buf_inventory_stop(buf_inventory);
//             doca_buf_inventory_destroy(buf_inventory);
//         }
//     }
// };

struct DocaBufDeleter {
    void
    operator()(struct doca_buf *buf) const noexcept {
        if (buf != nullptr) {
            doca_buf_dec_refcount(buf, nullptr);
        }
    }
};

// TODO: make the ptrs point to objects instead of raw pointers (other than list ptrs)
typedef std::unique_ptr<struct doca_dev, DocaDevDeleter> nixl_doca_dev_ptr;
typedef std::unique_ptr<struct doca_dev_rep, DocaRepDeleter> nixl_doca_dev_rep_ptr;
typedef std::unique_ptr<struct doca_ctx, DocaCtxDeleter> nixl_doca_ctx;
typedef std::unique_ptr<struct doca_pe, DocaPeDeleter> nixl_doca_pe_ptr;
typedef std::unique_ptr<struct doca_comch_server, DocaComchServerDeleter>
    nixl_doca_comch_server_ptr;
typedef std::unique_ptr<struct doca_comch_client, DocaComchClientDeleter>
    nixl_doca_comch_client_ptr;
typedef std::unique_ptr<struct doca_devinfo *, DocaDevListDeleter> nixl_doca_devinfo_list_ptr;
typedef std::unique_ptr<struct doca_comch_task_send, DocaComchTaskSendDeleter>
    nixl_doca_comch_task_send_ptr;
typedef std::unique_ptr<struct doca_comch_producer_task_send, DocaComchProducerTaskSendDeleter>
    nixl_doca_comch_producer_task_send_ptr;
typedef std::unique_ptr<struct doca_comch_producer, DocaComchProducerDeleter>
    nixl_doca_comch_producer_ptr;
typedef std::unique_ptr<struct doca_comch_consumer, DocaComchConsumerDeleter>
    nixl_doca_comch_consumer_ptr;
typedef std::unique_ptr<struct doca_mmap, DocaMmapDeleter> nixl_doca_mmap_ptr;
// typedef std::unique_ptr<struct doca_buf_inventory, DocaBufInventoryDeleter>
// nixl_doca_buf_inventory_ptr;
typedef std::unique_ptr<struct doca_buf, DocaBufDeleter> nixl_doca_buf_ptr;

// Callback function pointer types for producer/consumer
using ProducerSendCompletionCb = void (*)(struct doca_comch_producer_task_send *task,
                                          union doca_data task_user_data,
                                          union doca_data ctx_user_data);
using ProducerSendErrorCb = void (*)(struct doca_comch_producer_task_send *task,
                                     union doca_data task_user_data,
                                     union doca_data ctx_user_data);
using ConsumerPostRecvCompletionCb = void (*)(struct doca_comch_consumer_task_post_recv *task,
                                              union doca_data task_user_data,
                                              union doca_data ctx_user_data);
using ConsumerPostRecvErrorCb = void (*)(struct doca_comch_consumer_task_post_recv *task,
                                         union doca_data task_user_data,
                                         union doca_data ctx_user_data);

namespace doca_dev_utils {
// Polling intervals for context state transitions
constexpr auto kCtxStartPollInterval = std::chrono::milliseconds(10);
constexpr auto kCtxStopPollInterval = std::chrono::milliseconds(1);

/**
 * @brief Wait for a DOCA context to reach RUNNING state.
 *
 * Polls the progress engine and checks the context state until it reaches
 * RUNNING state or times out.
 *
 * @param ctx       The DOCA context to wait for
 * @param pe        The progress engine to poll
 * @param timeout   Maximum time to wait
 * @param ctx_name  Name for error messages (e.g., "consumer context")
 * @throws ComchError if timeout or invalid state
 */
void
wait_for_ctx_running(struct doca_ctx *ctx,
                     struct doca_pe *pe,
                     std::chrono::milliseconds timeout,
                     const std::string &ctx_name);

[[nodiscard]] nixl_doca_dev_ptr
open_doca_device_by_bdf(const std::string &bdf);
[[nodiscard]] nixl_doca_dev_rep_ptr
open_doca_dev_rep_by_bdf(const std::string &bdf);
[[nodiscard]] nixl_doca_pe_ptr
create_doca_pe();
std::string
doca_ctx_states_to_string(enum doca_ctx_states state);
[[nodiscard]] nixl_doca_mmap_ptr
create_configured_mmap(void *buf, size_t size, uint32_t access_flags, struct doca_dev *dev);

/**
 * @brief Create and start a DOCA comch producer.
 *
 * Creates the producer, connects it to the progress engine, configures
 * callbacks, and starts the context.
 *
 * @param conn              Connection to create producer on
 * @param pe                Progress engine to connect context to
 * @param on_complete       Completion callback for send tasks
 * @param on_error          Error callback for send tasks
 * @param num_tasks         Maximum number of concurrent tasks
 * @param out_producer_ctx  Output: producer context (for lifecycle management)
 * @return                  The created producer (caller owns)
 */
[[nodiscard]] struct doca_comch_producer *
create_producer(struct doca_comch_connection *conn,
                struct doca_pe *pe,
                ProducerSendCompletionCb on_complete,
                ProducerSendErrorCb on_error,
                uint32_t num_tasks,
                nixl_doca_ctx &out_producer_ctx);

/**
 * @brief Create and start a DOCA comch consumer.
 *
 * Creates the consumer, connects it to the progress engine, configures
 * callbacks and immediate data length, and starts the context.
 *
 * @param conn              Connection to create consumer on
 * @param pe                Progress engine to connect context to
 * @param mmap              Memory map for consumer buffers
 * @param on_complete       Completion callback for post_recv tasks
 * @param on_error          Error callback for post_recv tasks
 * @param num_tasks         Maximum number of concurrent tasks
 * @param imm_data_len      Immediate data length for consumer
 * @param out_consumer_ctx  Output: consumer context (for lifecycle management)
 * @return                  The created consumer (caller owns)
 */
[[nodiscard]] struct doca_comch_consumer *
create_consumer(struct doca_comch_connection *conn,
                struct doca_pe *pe,
                struct doca_mmap *mmap,
                ConsumerPostRecvCompletionCb on_complete,
                ConsumerPostRecvErrorCb on_error,
                uint32_t num_tasks,
                uint32_t imm_data_len,
                nixl_doca_ctx &out_consumer_ctx);

/**
 * @brief Create a consumer mmap with page-aligned memory.
 *
 * Allocates page-aligned memory, creates and configures an mmap,
 * and sets a free callback to release the memory on destruction.
 *
 * @param dev           Device to add to the mmap
 * @param mmap_size     Size of the memory region to allocate
 * @param access_flags  DOCA access flags for the mmap permissions
 * @param out_base      Output: base pointer of allocated memory
 * @return              The created mmap (caller owns)
 */
[[nodiscard]] nixl_doca_mmap_ptr
create_consumer_mmap(struct doca_dev *dev,
                     size_t mmap_size,
                     uint32_t access_flags,
                     void **out_base);

/**
 * @brief Create and start a buffer inventory.
 *
 * @param num_bufs  Number of buffers in the inventory
 * @return          The created buffer inventory (caller owns via raw pointer)
 */
[[nodiscard]] struct doca_buf_inventory *
create_buf_inventory(uint32_t num_bufs);

/**
 * @brief Submit post_recv tasks for a consumer.
 *
 * Divides the mmap memory equally among num_tasks buffers and submits
 * post_recv tasks for each.
 *
 * @param consumer      Consumer to submit tasks for
 * @param buf_inventory Buffer inventory to get buffers from
 * @param mmap          Memory map containing the buffers
 * @param mmap_base     Base address of the mmap memory
 * @param mmap_size     Size of the mmap memory
 * @param num_tasks     Number of post_recv tasks to submit
 */
void
submit_post_recv_tasks(struct doca_comch_consumer *consumer,
                       struct doca_buf_inventory *buf_inventory,
                       struct doca_mmap *mmap,
                       void *mmap_base,
                       size_t mmap_size,
                       uint32_t num_tasks);
} // namespace doca_dev_utils

#endif /* COMCH_COMMON_DOCA_DEV_UTILS_H */
