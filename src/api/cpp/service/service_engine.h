/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __SERVICE_ENGINE_H
#define __SERVICE_ENGINE_H

#include <string>
#include <vector>
#include "nixl_types.h"
#include "nixl_descriptors.h"

// Initialization parameters for service engine
struct nixlServiceInitParams {
    nixl_service_t type;                // Service type
    const nixl_s_params_t* customParams; // Custom parameters
};

// Base service engine class for different service implementations.
//
// Threading contract: the engine is purely synchronous and has no knowledge
// of callbacks or threads. nixl wraps each transfer request in a dedicated
// std::thread that calls processData() and, after it returns, initiates the
// backend transfer. Plugins must be thread-safe only in the sense that
// concurrent requests call processData() concurrently (serialising internally
// where needed, e.g. via a mutex around a single-threaded hardware context).
class nixlServiceEngine {
private:
    nixl_service_t serviceType_;
    nixl_mem_list_t inputMems_;
    nixl_mem_list_t outputMems_;
    nixl_s_params_t customParams_;

protected:
    bool initErr = false;

    [[nodiscard]] nixl_status_t
    setInitParam(const std::string &key, const std::string &value) {
        if (customParams_.emplace(key, value).second) {
            return NIXL_SUCCESS;
        }
        return NIXL_ERR_NOT_ALLOWED;
    }

    [[nodiscard]] nixl_status_t 
    getInitParam(const std::string &key, std::string &value) const {
        const auto iter = customParams_.find(key);
        if (iter != customParams_.end()) {
            value = iter->second;
            return NIXL_SUCCESS;
        }
        return NIXL_ERR_INVALID_PARAM;
    }

public:
    explicit nixlServiceEngine(const nixlServiceInitParams* init_params)
        : serviceType_(init_params->type),
          customParams_(init_params->customParams ? *init_params->customParams : nixl_s_params_t{}) {}

    nixlServiceEngine(nixlServiceEngine&&) = delete;
    nixlServiceEngine(const nixlServiceEngine&) = delete;
    void operator=(nixlServiceEngine&&) = delete;
    void operator=(const nixlServiceEngine&) = delete;

    virtual ~nixlServiceEngine() = default;

    bool getInitErr() const noexcept { return initErr; }
    const nixl_service_t& getType() const noexcept { return serviceType_; }
    const nixl_s_params_t& getCustomParams() const noexcept { return customParams_; }

    // *** Pure virtual methods that need to be implemented by any service *** //

    // Get supported input/output memory types
    const nixl_mem_list_t& getSupportedInputMems() const noexcept { return inputMems_; }
    const nixl_mem_list_t& getSupportedOutputMems() const noexcept { return outputMems_; }

    // Return the worst-case output buffer size for a given input size and operation.
    // op: NIXL_WRITE (encode path) or NIXL_READ (decode path)
    virtual size_t GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const = 0;

    // Perform the data transformation synchronously.
    // Blocks until the operation is fully complete and returns NIXL_SUCCESS,
    // or a negative error code on failure.
    //
    // Called from a dedicated per-request thread spawned by nixl. The engine
    // must serialize any internal hardware context accesses itself (e.g. via
    // a mutex) when multiple requests are processed concurrently.
    virtual nixl_status_t processData(const nixl_xfer_op_t &operation,
                                      const std::vector<nixlBlobDesc> &data_descs) = 0;

};

#endif // __SERVICE_ENGINE_H
