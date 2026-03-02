#include "host_client.h"
#include "comch_error.h"

#include <cstring>
#include <iostream>
#include <thread>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
extern "C" {
#include <doca_log.h>
DOCA_LOG_REGISTER(HOST_CLIENT);
}
#pragma GCC diagnostic pop

HostClient *HostClient::instance_ = nullptr;

HostClient::HostClient(const std::string &dev_bdf,
                       const std::string &server_name,
                       uint32_t num_tasks,
                       std::chrono::milliseconds timeout)
    : dev_bdf_(dev_bdf),
      server_name_(server_name),
      num_tasks_(num_tasks),
      timeout_(timeout),
      connection_ptr_(nullptr),
      client_running_(false),
      client_failed_(false),
      task_id_(0),
      client_state_(ClientState::CLIENT_STATE_PRE_START),
      producer_(nullptr),
      consumer_(nullptr),
      consumer_mmap_base_(nullptr),
      buf_inventory_(nullptr) {
    if (dev_bdf_.empty() || server_name_.empty()) {
        throw ComchError(ComchErrorCode::COMCH_INVALID_ARG,
                         "dev_bdf and server_name cannot be empty");
    }

    if (instance_ != nullptr) {
        throw ComchError(ComchErrorCode::COMCH_INVALID_STATE,
                         "Only one HostClient instance allowed at a time");
    }
    instance_ = this;

    dev_ptr_ = doca_dev_utils::open_doca_device_by_bdf(dev_bdf_);
    InitializeDocaComchClient();

    ctx_.reset(doca_comch_client_as_ctx(comch_client_ptr_.get()));
    if (ctx_ == nullptr)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR, "doca_comch_client_as_ctx failed");

    doca_error_t rc = doca_ctx_set_state_changed_cb(ctx_.get(), client_state_changed_cb);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_ctx_set_state_changed_cb failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_comch_client_event_msg_recv_register(comch_client_ptr_.get(), on_msg_recv_cb);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_client_event_msg_recv_register failed: " +
                             std::string(doca_error_get_descr(rc)));

    rc = doca_comch_client_task_send_set_conf(
        comch_client_ptr_.get(), send_task_completion_cb, send_task_error_cb, num_tasks_);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_client_task_send_set_conf failed: " +
                             std::string(doca_error_get_descr(rc)));

    pe_ptr_ = doca_dev_utils::create_doca_pe();
    rc = doca_pe_connect_ctx(pe_ptr_.get(), ctx_.get());
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_pe_connect_ctx failed: " + std::string(doca_error_get_descr(rc)));
}

HostClient::~HostClient() {
    CleanupProducerConsumer();
    instance_ = nullptr;
}

void
HostClient::start() {
    doca_error_t rc = doca_ctx_start(ctx_.get());
    if (rc != DOCA_SUCCESS && rc != DOCA_ERROR_IN_PROGRESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_ctx_start failed: " + std::string(doca_error_get_descr(rc)));

    doca_dev_utils::wait_for_ctx_running(ctx_.get(), pe_ptr_.get(), timeout_, "client context");

    if (!client_running_) {
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR, "Client context failed to start");
    }
    rc = doca_comch_client_get_connection(comch_client_ptr_.get(), &connection_ptr_);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_client_get_connection failed: " +
                             std::string(doca_error_get_descr(rc)));

    // Need buffers for both consumer post_recv tasks AND producer send tasks
    buf_inventory_ = doca_dev_utils::create_buf_inventory(num_tasks_ * 2);

    uint32_t access_flags = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_PCI_READ_WRITE;
    consumer_mmap_ = doca_dev_utils::create_consumer_mmap(
        dev_ptr_.get(), COMCH_CLIENT_CONSUMER_MMAP_SIZE, access_flags, &consumer_mmap_base_);

    producer_ = doca_dev_utils::create_producer(connection_ptr_,
                                                pe_ptr_.get(),
                                                producer_send_completion_cb,
                                                producer_send_error_cb,
                                                num_tasks_,
                                                producer_ctx_);

    doca_dev_utils::wait_for_ctx_running(
        producer_ctx_.get(), pe_ptr_.get(), timeout_, "producer context");

    consumer_ = doca_dev_utils::create_consumer(connection_ptr_,
                                                pe_ptr_.get(),
                                                consumer_mmap_.get(),
                                                consumer_post_recv_completion_cb,
                                                consumer_post_recv_error_cb,
                                                num_tasks_,
                                                COMCH_CONSUMER_IMM_DATA_LEN,
                                                consumer_ctx_);

    doca_dev_utils::wait_for_ctx_running(
        consumer_ctx_.get(), pe_ptr_.get(), timeout_, "consumer context");

    doca_dev_utils::submit_post_recv_tasks(consumer_,
                                           buf_inventory_,
                                           consumer_mmap_.get(),
                                           consumer_mmap_base_,
                                           COMCH_CLIENT_CONSUMER_MMAP_SIZE,
                                           num_tasks_);

    rc = doca_comch_consumer_get_id(consumer_, &consumer_id_);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_consumer_get_id failed: " +
                             std::string(doca_error_get_descr(rc)));

    ControlReqHandle control_req_handle = {.control_message_type =
                                               ControlMessageType::CONTROL_MESSAGE_TYPE_HANDSHAKE,
                                           .handshake_data = {.consumer_id = consumer_id_}};

    struct doca_comch_task_send *send_task;
    rc = doca_comch_client_task_send_alloc_init(comch_client_ptr_.get(),
                                                connection_ptr_,
                                                &control_req_handle,
                                                sizeof(ControlReqHandle),
                                                &send_task);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_client_task_send_alloc_init failed: " +
                             std::string(doca_error_get_descr(rc)));
    nixl_doca_comch_task_send_ptr send_task_ptr(send_task);

    doca_task *doca_send_task = doca_comch_task_send_as_task(send_task);
    rc = doca_task_submit(doca_send_task);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_task_submit (client handshake) failed: " +
                             std::string(doca_error_get_descr(rc)));

    send_task_ptr.release();

    client_state_ = ClientState::CLIENT_STATE_HANDSHAKE_PENDING;
}

bool
HostClient::poll() {
    // doca_pe_progress returns non-zero if work was done, 0 if no work
    return doca_pe_progress(pe_ptr_.get()) != 0;
}

void
HostClient::CreateAndSubmitCompSendTask(void *source_buf,
                                        std::size_t source_size,
                                        void *dest_buf,
                                        CompType comp_type) {

    if (client_state_ != ClientState::CLIENT_STATE_RUNNING)
        throw ComchError(ComchErrorCode::COMCH_INVALID_STATE,
                         "Client is not running. Current state: " +
                             ClientStateToString(client_state_));

    if (source_buf == nullptr || source_size == 0 || dest_buf == nullptr ||
        !IsValidCompType(comp_type))
        throw ComchError(
            ComchErrorCode::COMCH_INVALID_ARG,
            "source_buf, source_size, dest_buf, and comp_type cannot be empty or null");

    task_id_t comp_task_id = GetAndIncrementTaskId();

    auto [it, inserted] = comp_req_handles_.emplace(
        comp_task_id, CompReqHandle(source_buf, source_size, comp_task_id, comp_type, dest_buf));
    CompReqHandle &handle = it->second;
    uint32_t access_flags = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_PCI_READ_WRITE;
    nixl_doca_mmap_ptr local_mmap_ptr = doca_dev_utils::create_configured_mmap(
        source_buf, source_size, access_flags, dev_ptr_.get());

    struct doca_buf *src_doca_buf;
    doca_error_t rc = doca_buf_inventory_buf_get_by_data(
        buf_inventory_, local_mmap_ptr.get(), source_buf, source_size, &src_doca_buf);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_buf_inventory_buf_by_data failed: " +
                             std::string(doca_error_get_descr(rc)));
    nixl_doca_buf_ptr src_doca_buf_ptr(src_doca_buf);

    struct doca_comch_producer_task_send *send_task;
    uint8_t *task_data = reinterpret_cast<uint8_t *>(&handle);
    size_t task_data_size = sizeof(handle);
    rc = doca_comch_producer_task_send_alloc_init(
        producer_, src_doca_buf, task_data, task_data_size, server_consumer_id_, &send_task);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_producer_task_send_alloc_init failed: " +
                             std::string(doca_error_get_descr(rc)));
    nixl_doca_comch_producer_task_send_ptr send_task_ptr(send_task);

    doca_comch_producer_task_send_set_buf(send_task, src_doca_buf);

    doca_comch_producer_task_send_set_consumer_id(send_task, server_consumer_id_);

    doca_data user_data = {.u64 = comp_task_id};
    doca_task_set_user_data(doca_comch_producer_task_send_as_task(send_task), user_data);

    task_resources_.emplace(
        comp_task_id,
        std::make_unique<TaskResources>(local_mmap_ptr.release(), src_doca_buf_ptr.release()));

    handle.state = ReqState::REQ_STATE_SUBMITTED;
    rc = doca_task_submit(doca_comch_producer_task_send_as_task(send_task));
    if (rc != DOCA_SUCCESS) {
        handle.state = ReqState::REQ_STATE_FAILED;
        task_resources_.erase(comp_task_id);
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_task_submit failed: " + std::string(doca_error_get_descr(rc)));
    }
    send_task_ptr.release();
}

HostClient::ClientState
HostClient::GetState() const noexcept {
    return client_state_;
}

void
HostClient::InitializeDocaComchClient() {
    struct doca_comch_client *comch_client_ptr_raw;
    doca_error_t rc =
        doca_comch_client_create(dev_ptr_.get(), server_name_.c_str(), &comch_client_ptr_raw);
    comch_client_ptr_.reset(comch_client_ptr_raw);
    if (rc != DOCA_SUCCESS)
        throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                         "doca_comch_client_create failed: " +
                             std::string(doca_error_get_descr(rc)));
}

void
HostClient::client_state_changed_cb(union doca_data user_data,
                                    struct doca_ctx *ctx,
                                    enum doca_ctx_states prev_state,
                                    enum doca_ctx_states next_state) {
    (void)user_data;
    (void)ctx;
    (void)prev_state;

    if (instance_ == nullptr) return;

    if (next_state == DOCA_CTX_STATE_RUNNING) {
        instance_->client_running_ = true;
    } else if (next_state != DOCA_CTX_STATE_IDLE) {
        instance_->client_failed_ = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        DOCA_LOG_ERR("Client failed to start: %s",
                     doca_dev_utils::doca_ctx_states_to_string(next_state).c_str());
#pragma GCC diagnostic pop
    }
}

void
HostClient::on_msg_recv_cb(struct doca_comch_event_msg_recv *event,
                           uint8_t *recv_buffer,
                           uint32_t msg_len,
                           struct doca_comch_connection *comch_connection) {
    (void)event;
    (void)comch_connection;
    (void)msg_len;

    if (instance_ == nullptr) return;

    const ControlReqHandle *control_req_handle =
        reinterpret_cast<const ControlReqHandle *>(recv_buffer);
    if (control_req_handle == nullptr) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        DOCA_LOG_ERR("Invalid control request handle");
#pragma GCC diagnostic pop
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    DOCA_LOG_INFO("on_msg_recv_cb: received control message type: %s",
                  to_string(control_req_handle->control_message_type).c_str());
#pragma GCC diagnostic pop

    switch (control_req_handle->control_message_type) {
    case ControlMessageType::CONTROL_MESSAGE_TYPE_HANDSHAKE:
        instance_->HandleHandshakeControlMessage(&control_req_handle->handshake_data);
        break;
    default:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        DOCA_LOG_ERR("Unknown control message type: %s",
                     to_string(control_req_handle->control_message_type).c_str());
#pragma GCC diagnostic pop
        break;
    }
}

void
HostClient::send_task_completion_cb(struct doca_comch_task_send *task,
                                    union doca_data task_user_data,
                                    union doca_data ctx_user_data) {
    (void)task_user_data;
    (void)ctx_user_data;
    doca_task_free(doca_comch_task_send_as_task(task));
}

void
HostClient::send_task_error_cb(struct doca_comch_task_send *task,
                               union doca_data task_user_data,
                               union doca_data ctx_user_data) {
    (void)task_user_data;
    (void)ctx_user_data;
    doca_error_t status = doca_task_get_status(doca_comch_task_send_as_task(task));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    DOCA_LOG_ERR("Client send task failed: %s", doca_error_get_descr(status));
#pragma GCC diagnostic pop
    doca_task_free(doca_comch_task_send_as_task(task));
}

void
HostClient::HandleHandshakeControlMessage(const HandshakeData *handshake_data) {
    server_consumer_id_ = handshake_data->consumer_id;
    client_state_ = ClientState::CLIENT_STATE_RUNNING;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    DOCA_LOG_INFO("Handshake received from server with consumer id: %d", server_consumer_id_);
#pragma GCC diagnostic pop
}

task_id_t
HostClient::GetAndIncrementTaskId() noexcept {
    return task_id_.fetch_add(1);
}

void
HostClient::CleanupProducerConsumer() {
    // Stop contexts first
    producer_ctx_.reset();
    consumer_ctx_.reset();

    if (producer_ != nullptr) {
        doca_comch_producer_destroy(producer_);
        producer_ = nullptr;
    }
    if (consumer_ != nullptr) {
        doca_comch_consumer_destroy(consumer_);
        consumer_ = nullptr;
    }
    if (buf_inventory_ != nullptr) {
        doca_buf_inventory_stop(buf_inventory_);
        doca_buf_inventory_destroy(buf_inventory_);
        buf_inventory_ = nullptr;
    }
    // mmap destruction will call the free callback to release consumer_mmap_base_
    consumer_mmap_.reset();
    consumer_mmap_base_ = nullptr;
}

void
HostClient::producer_send_completion_cb(struct doca_comch_producer_task_send *task,
                                        union doca_data task_user_data,
                                        union doca_data ctx_user_data) {
    (void)ctx_user_data;

    std::cout << "[DEBUG] Client producer_send_completion_cb called!" << std::endl;

    task_id_t task_id = static_cast<task_id_t>(task_user_data.u64);

    auto res_it = instance_->task_resources_.find(task_id);
    if (res_it != instance_->task_resources_.end()) instance_->task_resources_.erase(res_it);

    doca_task_free(doca_comch_producer_task_send_as_task(task));

    auto handle_it = instance_->comp_req_handles_.find(task_id);
    if (handle_it != instance_->comp_req_handles_.end())
        handle_it->second.state = ReqState::REQ_STATE_SENT;
}

void
HostClient::producer_send_error_cb(struct doca_comch_producer_task_send *task,
                                   union doca_data task_user_data,
                                   union doca_data ctx_user_data) {
    (void)ctx_user_data;

    task_id_t task_id = static_cast<task_id_t>(task_user_data.u64);

    auto res_it = instance_->task_resources_.find(task_id);
    if (res_it != instance_->task_resources_.end()) instance_->task_resources_.erase(res_it);

    doca_task_free(doca_comch_producer_task_send_as_task(task));

    auto handle_it = instance_->comp_req_handles_.find(task_id);
    if (handle_it != instance_->comp_req_handles_.end())
        handle_it->second.state = ReqState::REQ_STATE_SEND_FAILED;
}

void
HostClient::consumer_post_recv_completion_cb(struct doca_comch_consumer_task_post_recv *task,
                                             union doca_data task_user_data,
                                             union doca_data ctx_user_data) {
    (void)task_user_data;
    (void)ctx_user_data;

    try {
        const uint8_t *imm_data = doca_comch_consumer_task_post_recv_get_imm_data(task);
        uint32_t imm_data_len = doca_comch_consumer_task_post_recv_get_imm_data_len(task);
        if (imm_data == nullptr || imm_data_len < sizeof(CompReqHandle))
            throw ComchError(ComchErrorCode::COMCH_INVALID_ARG, "Invalid immediate data");

        const CompReqHandle *resp_handle = reinterpret_cast<const CompReqHandle *>(imm_data);

        struct doca_buf *recv_buf = doca_comch_consumer_task_post_recv_get_buf(task);
        if (recv_buf == nullptr)
            throw ComchError(ComchErrorCode::COMCH_INVALID_ARG, "No buffer received");

        void *compressed_data;
        doca_error_t rc = doca_buf_get_data(recv_buf, &compressed_data);
        if (rc != DOCA_SUCCESS || compressed_data == nullptr)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR, "Failed to get buffer data");

        size_t compressed_len;
        rc = doca_buf_get_data_len(recv_buf, &compressed_len);
        if (rc != DOCA_SUCCESS)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "Failed to get buffer data length");

        // Copy compressed data to client's destination buffer
        std::memcpy(const_cast<void *>(resp_handle->dest_buf), compressed_data, compressed_len);

        // Update the client's handle state
        auto handle_it = instance_->comp_req_handles_.find(resp_handle->task_id);
        if (handle_it != instance_->comp_req_handles_.end())
            handle_it->second.state = resp_handle->state;

        // Resubmit the post_recv task to continue receiving
        doca_comch_consumer_task_post_recv_set_buf(task, recv_buf);
        rc = doca_task_submit(doca_comch_consumer_task_post_recv_as_task(task));
        if (rc != DOCA_SUCCESS)
            throw ComchError(ComchErrorCode::COMCH_INTERNAL_ERROR,
                             "Failed to resubmit post_recv task");
    }
    catch (const ComchError &e) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        DOCA_LOG_ERR("Consumer post_recv completion callback failed: %s", e.what());
#pragma GCC diagnostic pop
    }
}

void
HostClient::consumer_post_recv_error_cb(struct doca_comch_consumer_task_post_recv *task,
                                        union doca_data task_user_data,
                                        union doca_data ctx_user_data) {
    (void)task_user_data;
    (void)ctx_user_data;

    doca_error_t status = doca_task_get_status(doca_comch_consumer_task_post_recv_as_task(task));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    DOCA_LOG_ERR("Consumer post_recv task failed: %s", doca_error_get_descr(status));
#pragma GCC diagnostic pop
}
