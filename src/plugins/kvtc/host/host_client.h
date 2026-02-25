#ifndef COMCH_HOST_HOST_CLIENT_H
#define COMCH_HOST_HOST_CLIENT_H

#include <atomic>
#include <chrono>
#include <map>
#include <string>

#include "doca_dev_utils.h"
#include "comp_req.h"
#include "doca_mmap.h"
#include "doca_buf.h"

constexpr size_t COMCH_CLIENT_CONSUMER_MMAP_SIZE =
    8UL * 1024UL * 1024UL; // 8MB (2MB per task * 4 tasks, within consumer_max_buf_size)

struct ClientCompReqHandle {
    CompReqHandle comp_req_handle;
    nixl_doca_comch_task_send_ptr send_req_ptr;
};

struct TaskResources {
    struct doca_mmap *mmap;
    struct doca_buf *buf;

    TaskResources(struct doca_mmap *mmap, struct doca_buf *buf) : mmap(mmap), buf(buf) {}

    ~TaskResources() {
        if (buf != nullptr) doca_buf_dec_refcount(buf, nullptr);
        if (mmap != nullptr) doca_mmap_destroy(mmap);
    }
};

class HostClient {
public:
    enum class ClientState {
        CLIENT_STATE_PRE_START,
        CLIENT_STATE_HANDSHAKE_PENDING, // Sent handshake, waiting for server response
        CLIENT_STATE_RUNNING,
        // CLIENT_STATE_STOPPING,
        // CLIENT_STATE_STOPPED,
        // CLIENT_STATE_ERROR,
    };

    static std::string
    ClientStateToString(ClientState state) {
        switch (state) {
        case HostClient::ClientState::CLIENT_STATE_PRE_START:
            return "CLIENT_STATE_PRE_START";
        case HostClient::ClientState::CLIENT_STATE_HANDSHAKE_PENDING:
            return "CLIENT_STATE_HANDSHAKE_PENDING";
        case HostClient::ClientState::CLIENT_STATE_RUNNING:
            return "CLIENT_STATE_RUNNING";
        // case HostClient::ClientState::CLIENT_STATE_STOPPING:
        //     return "CLIENT_STATE_STOPPING";
        // case HostClient::ClientState::CLIENT_STATE_STOPPED:
        //     return "CLIENT_STATE_STOPPED";
        // case HostClient::ClientState::CLIENT_STATE_ERROR:
        //     return "CLIENT_STATE_ERROR";
        default:
            return "UNRECOGNIZED_CLIENT_STATE: " + std::to_string(static_cast<int>(state));
        }
    }

    HostClient(const std::string &dev_bdf,
               const std::string &server_name,
               uint32_t num_tasks,
               std::chrono::milliseconds timeout);
    ~HostClient();
    void
    start();
    bool
    poll(); // Progress the DOCA PE to process events. Returns true if work was done, false otherwise.
    void
    CreateAndSubmitCompSendTask(void *source_buf,
                                std::size_t source_size,
                                void *dest_buf,
                                CompType comp_type);
    ClientState
    GetState() const noexcept;
    // void stop();

protected:
    // static pointer to the singleton instance of the class (used in callback functions)
    static HostClient *instance_;

    const std::string dev_bdf_;
    const std::string server_name_;
    const uint32_t num_tasks_;
    const std::chrono::milliseconds timeout_;

    // DOCA resources
    nixl_doca_dev_ptr dev_ptr_;
    nixl_doca_pe_ptr pe_ptr_;
    nixl_doca_comch_client_ptr comch_client_ptr_;
    nixl_doca_ctx ctx_;
    // doesnt need to be unique_ptr because it does not need cleanup
    struct doca_comch_connection *connection_ptr_;
    std::atomic<bool> client_running_;
    std::atomic<bool> client_failed_;
    std::atomic<task_id_t> task_id_;
    std::map<task_id_t, CompReqHandle> comp_req_handles_;
    std::atomic<ClientState> client_state_;
    std::map<task_id_t, std::unique_ptr<TaskResources>> task_resources_;

    // Producer resources
    struct doca_comch_producer *producer_;
    nixl_doca_ctx producer_ctx_;

    // Consumer resources (for receiving responses)
    struct doca_comch_consumer *consumer_;
    uint32_t consumer_id_;
    nixl_doca_ctx consumer_ctx_;
    nixl_doca_mmap_ptr consumer_mmap_;
    void *consumer_mmap_base_;
    struct doca_buf_inventory *buf_inventory_;

    // Server resources
    uint32_t server_consumer_id_;

    void
    InitializeDocaComchClient();
    task_id_t
    GetAndIncrementTaskId() noexcept;
    void
    CleanupProducerConsumer();

    // control message handling functions
    void
    HandleHandshakeControlMessage(const HandshakeData *handshake_data);

    // control message callbacks
    static void
    client_state_changed_cb(union doca_data user_data,
                            struct doca_ctx *ctx,
                            enum doca_ctx_states prev_state,
                            enum doca_ctx_states next_state);

    static void
    on_msg_recv_cb(struct doca_comch_event_msg_recv *event,
                   uint8_t *recv_buffer,
                   uint32_t msg_len,
                   struct doca_comch_connection *comch_connection);

    // Control channel send task callbacks
    static void
    send_task_completion_cb(struct doca_comch_task_send *task,
                            union doca_data task_user_data,
                            union doca_data ctx_user_data);
    static void
    send_task_error_cb(struct doca_comch_task_send *task,
                       union doca_data task_user_data,
                       union doca_data ctx_user_data);

    // Producer callbacks
    static void
    producer_send_completion_cb(struct doca_comch_producer_task_send *task,
                                union doca_data task_user_data,
                                union doca_data ctx_user_data);

    static void
    producer_send_error_cb(struct doca_comch_producer_task_send *task,
                           union doca_data task_user_data,
                           union doca_data ctx_user_data);

    // Consumer callbacks
    static void
    consumer_post_recv_completion_cb(struct doca_comch_consumer_task_post_recv *task,
                                     union doca_data task_user_data,
                                     union doca_data ctx_user_data);

    static void
    consumer_post_recv_error_cb(struct doca_comch_consumer_task_post_recv *task,
                                union doca_data task_user_data,
                                union doca_data ctx_user_data);
};

#endif /* COMCH_HOST_HOST_CLIENT_H */
