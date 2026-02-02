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

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

#include "nixl_types.h"
#include "nixl_descriptors.h"
#include "nixl_log.h"

// Service operation flags
enum nixl_service_flags_t {
    NIXL_SERVICE_INPLACE = 1 << 0,      // Operate in-place (no temporary buffer)
    NIXL_SERVICE_HOST = 1 << 1,         // Execute on HOST
    NIXL_SERVICE_DPU = 1 << 2,          // Execute on DPU
};

// Initialization parameters for service engine
struct nixlServiceInitParams {
    nixl_service_t type;                // Service type
    uint32_t flags;                     // Service flags (in-place, host/DPU)
    const nixl_b_params_t* customParams; // Custom parameters
};

// Descriptor for processed data buffer
struct nixlServiceBufferDesc {
    void* addr;                         // Buffer address
    size_t len;                         // Buffer length
    nixl_mem_t mem_type;                // Memory type
    bool is_out_of_place;                  // True if buffer needs cleanup by service
};

// Base service engine class for different service implementations
class nixlServiceEngine {
private:
    nixl_service_t serviceType_;
    nixl_b_params_t customParams_;
    uint32_t flags_;

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
          customParams_(init_params->customParams ? *init_params->customParams : nixl_b_params_t{}),
          flags_(init_params->flags) {}

    nixlServiceEngine(nixlServiceEngine&&) = delete;
    nixlServiceEngine(const nixlServiceEngine&) = delete;
    void operator=(nixlServiceEngine&&) = delete;
    void operator=(const nixlServiceEngine&) = delete;

    virtual ~nixlServiceEngine() = default;

    bool getInitErr() const noexcept { return initErr; }
    const nixl_service_t& getType() const noexcept { return serviceType_; }
    uint32_t getFlags() const noexcept { return flags_; }
    const nixl_b_params_t& getCustomParams() const noexcept { return customParams_; }

    // Check if service supports in-place operation
    bool supportsInplace() const noexcept {
        return (flags_ & NIXL_SERVICE_INPLACE) != 0;
    }

    // *** Pure virtual methods that need to be implemented by any service *** //

    // Get supported memory types
    virtual nixl_mem_list_t getSupportedMems() const = 0;

    // // Register memory with the service (optional, may be needed for some services)
    // virtual nixl_status_t registerMem(const nixlBlobDesc &mem,
    //                                  const nixl_mem_t &nixl_mem,
    //                                  void* &service_context) {
    //     service_context = nullptr;
    //     return NIXL_SUCCESS; // Default: no registration needed
    // }

    // // Deregister memory
    // virtual nixl_status_t deregisterMem(void* service_context) {
    //     return NIXL_SUCCESS; // Default: no deregistration needed
    // }

    // Process data buffers based on operation
    // operation: NIXL_WRITE means compress/encode, NIXL_READ means decompress/decode
    virtual nixl_status_t processData(const nixl_xfer_op_t &operation,
                                     const std::vector<nixlBlobDesc> &data_descs) = 0;

    // Clean up temporary buffers allocated during processing
    // virtual nixl_status_t cleanupBuffers(nixlServiceResult &result) = 0;
};

#endif // __SERVICE_ENGINE_H
