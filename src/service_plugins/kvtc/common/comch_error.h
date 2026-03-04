#ifndef COMCH_COMMON_COMCH_ERROR_H
#define COMCH_COMMON_COMCH_ERROR_H

#include <stdexcept>
#include <string>
#include <utility>

enum class ComchErrorCode {
    COMCH_SUCCESS = 0,
    COMCH_INVALID_ARG,
    COMCH_NOT_CONNECTED,
    COMCH_NOT_FOUND,
    COMCH_INTERNAL_ERROR,
    COMCH_CALLBACK_REGISTRATION_FAILED,
    COMCH_INVALID_STATE,
    COMCH_UNKNOWN
};

inline std::string
to_string(ComchErrorCode code) {
    switch (code) {
    case ComchErrorCode::COMCH_SUCCESS:
        return "COMCH_SUCCESS";
    case ComchErrorCode::COMCH_INVALID_ARG:
        return "COMCH_INVALID_ARG";
    case ComchErrorCode::COMCH_NOT_CONNECTED:
        return "COMCH_NOT_CONNECTED";
    case ComchErrorCode::COMCH_NOT_FOUND:
        return "COMCH_NOT_FOUND";
    case ComchErrorCode::COMCH_INTERNAL_ERROR:
        return "COMCH_INTERNAL_ERROR";
    case ComchErrorCode::COMCH_CALLBACK_REGISTRATION_FAILED:
        return "COMCH_CALLBACK_REGISTRATION_FAILED";
    case ComchErrorCode::COMCH_INVALID_STATE:
        return "COMCH_INVALID_STATE";
    case ComchErrorCode::COMCH_UNKNOWN:
        return "COMCH_UNKNOWN";
    default:
        return "UNRECOGNIZED_COMCH_ERROR_CODE: " + std::to_string(static_cast<int>(code));
    }
}

struct ComchError : public std::runtime_error {
    const ComchErrorCode code;
    const std::string description;

    ComchError(ComchErrorCode code_in, std::string description_in = {})
        : std::runtime_error(description_in.empty() ? to_string(code_in) :
                                                      to_string(code_in) + ": " + description_in),
          code(code_in),
          description(std::move(description_in)) {}
};

#endif /* COMCH_COMMON_COMCH_ERROR_H */
