#ifndef COMCH_COMMON_COMP_REQ_H
#define COMCH_COMMON_COMP_REQ_H

#include <cstddef>
#include <string>

#include "doca_dev_utils.h"

#define COMCH_STRINGIFY(x) #x
#define COMCH_TOSTRING(x) COMCH_STRINGIFY(x)
#define REQ_HANDLE_MAX_SIZE 80
#define COMCH_CONSUMER_IMM_DATA_LEN REQ_HANDLE_MAX_SIZE

enum class ReqState {
    REQ_STATE_SUCCESS = 0,
    REQ_STATE_FAILED,
    REQ_STATE_PENDING,
    REQ_STATE_SUBMITTED,
    REQ_STATE_SENT,
    REQ_STATE_SEND_FAILED,
    REQ_STATE_UNKNOWN,
};

inline const std::string
to_string(ReqState state) {
    switch (state) {
    case ReqState::REQ_STATE_SUCCESS:
        return "REQ_STATE_SUCCESS";
    case ReqState::REQ_STATE_FAILED:
        return "REQ_STATE_FAILED";
    case ReqState::REQ_STATE_PENDING:
        return "REQ_STATE_PENDING";
    case ReqState::REQ_STATE_SUBMITTED:
        return "REQ_STATE_SUBMITTED";
    case ReqState::REQ_STATE_SENT:
        return "REQ_STATE_SENT";
    case ReqState::REQ_STATE_SEND_FAILED:
        return "REQ_STATE_SEND_FAILED";
    case ReqState::REQ_STATE_UNKNOWN:
        return "REQ_STATE_UNKNOWN";
    default:
        return "UNRECOGNIZED_REQ_STATE: " + std::to_string(static_cast<int>(state));
    }
}

enum class CompType {
    COMP_TYPE_KVTC_X16,
    COMP_TYPE_KVTC_X32,
    COMP_TYPE_CAT_X2,
};

inline const std::string
to_string(CompType comp_type) noexcept {
    switch (comp_type) {
    case CompType::COMP_TYPE_KVTC_X16:
        return "COMP_TYPE_KVTC_X16";
    case CompType::COMP_TYPE_KVTC_X32:
        return "COMP_TYPE_KVTC_X32";
    case CompType::COMP_TYPE_CAT_X2:
        return "COMP_TYPE_CAT_X2";
    default:
        return "UNRECOGNIZED_COMP_TYPE: " + std::to_string(static_cast<int>(comp_type));
    }
}

inline bool
IsValidCompType(CompType comp_type) noexcept {
    switch (comp_type) {
    case CompType::COMP_TYPE_KVTC_X16:
    case CompType::COMP_TYPE_KVTC_X32:
    case CompType::COMP_TYPE_CAT_X2:
        return true;
    default:
        return false;
    }
}

// TODO add a static assert for the size of the HandshakeData

struct CompReqHandle {
    const void *source_buf;
    const std::size_t source_size;
    const task_id_t task_id;
    const CompType comp_type;
    const void *dest_buf;
    nixl_doca_comch_task_send_ptr send_req_ptr;
    ReqState state;

    CompReqHandle(void *source_buf,
                  std::size_t source_size,
                  task_id_t task_id,
                  CompType comp_type,
                  void *dest_buf)
        : source_buf(source_buf),
          source_size(source_size),
          task_id(task_id),
          comp_type(comp_type),
          dest_buf(dest_buf),
          state(ReqState::REQ_STATE_PENDING) {}
};

static_assert(sizeof(CompReqHandle) <= REQ_HANDLE_MAX_SIZE,
              "CompReqHandle must be <= " COMCH_TOSTRING(REQ_HANDLE_MAX_SIZE) " bytes");

enum class ControlMessageType {
    CONTROL_MESSAGE_TYPE_HANDSHAKE,
    CONTROL_MESSAGE_TYPE_STOP,
};

inline const std::string
to_string(ControlMessageType control_message_type) noexcept {
    switch (control_message_type) {
    case ControlMessageType::CONTROL_MESSAGE_TYPE_HANDSHAKE:
        return "CONTROL_MESSAGE_TYPE_HANDSHAKE";
    case ControlMessageType::CONTROL_MESSAGE_TYPE_STOP:
        return "CONTROL_MESSAGE_TYPE_STOP";
    default:
        return "UNRECOGNIZED_CONTROL_MESSAGE_TYPE: " +
            std::to_string(static_cast<int>(control_message_type));
    }
}

struct HandshakeData {
    uint32_t consumer_id;
};

struct ControlReqHandle {
    const ControlMessageType control_message_type;

    union {
        HandshakeData handshake_data;
        // Add types for other control messages here
    };
};

static_assert(sizeof(ControlReqHandle) <= REQ_HANDLE_MAX_SIZE,
              "ControlReqHandle must be <= " COMCH_TOSTRING(REQ_HANDLE_MAX_SIZE) " bytes");

#endif /* COMCH_COMMON_COMP_REQ_H */
