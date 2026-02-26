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

// Initialization parameters for service engine
struct nixlServiceInitParams {
    nixl_service_t type;                // Service type
    uint32_t flags;                     // Service flags
    const nixl_b_params_t* customParams; // Custom parameters
};

// Base service engine class for different service implementations
class nixlServiceEngine {
private:
    nixl_service_t serviceType_;
    nixl_b_params_t customParams_;

protected:
    bool initErr = false;
    uint32_t flags_;

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

    // Process data buffers based on operation
    virtual nixl_status_t processData(const nixl_xfer_op_t &operation,
                                     const std::vector<nixlBlobDesc> &data_descs,
                                     const std::vector<nixlBlobDesc> &processed_data_descs) = 0;

};

#endif // __SERVICE_ENGINE_H
