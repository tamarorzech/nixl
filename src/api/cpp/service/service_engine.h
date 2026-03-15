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
    nixl_service_t type;                 // Service type
    const nixl_s_params_t* customParams; // Custom parameters
    nixl_service_mems_t mems;            // Supported input/output memory types
};

// Base service engine class for different service implementations.
//
// Threading contract:
//   Each service instance is thread-safe and non-blocking. Engines have no internal
//   worker threads. The agent's service PT pool (configured via service_enable_pt and
//   service_progress_threads in nixlAgentConfig) drives progress for all service
//   requests across all services.
//
// Agent-PT path (service_enable_pt == true):
//   postXferReq calls processDataAsync() (non-blocking) to dispatch work, then enqueues
//   the request. The agent's shared PT pool loops calling poll() for every pending
//   request; when poll returns NIXL_SUCCESS the thread immediately calls backend
//   postXfer() in the same context (fluent handoff).
//   The engine must be safe to call processDataAsync() from the agent thread and poll()
//   from the progress thread concurrently.
//
// External-polling path (service_enable_pt == false):
//   postXferReq calls processDataAsync() (non-blocking). Each call to getXferStatus()
//   calls poll() once to advance the service state machine. On NIXL_SUCCESS,
//   getXferStatus immediately calls backend postXfer() (fluent handoff).
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
          inputMems_(init_params->mems.input),
          outputMems_(init_params->mems.output),
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

    // Non-blocking submission. Called on the agent thread from postXferReq.
    // Must dispatch work to hardware and return immediately.
    // Fills out_descs with pre-allocated output buffer descriptors (at minimum
    // worst-case sizes); the engine may update them in poll() on completion.
    // svc_req_out is an opaque handle that uniquely identifies this request for
    // all subsequent poll() calls.
    virtual nixl_status_t processDataAsync(const nixl_xfer_op_t &operation,
                                       const std::vector<nixlBlobDesc> &src_descs,
                                       std::vector<nixlBlobDesc> &out_descs,
                                       const nixl_s_params_t *service_meta,
                                       uint64_t &svc_req_out) = 0;

    // Non-blocking progress tick for one request. Returns NIXL_IN_PROG while busy,
    // NIXL_SUCCESS when done, or a negative error code on failure.
    // On NIXL_SUCCESS the engine may update out_descs with actual output sizes or
    // addresses (e.g. actual compressed size reported by hardware).
    // Called from the agent's service PT pool (agent-PT path) or from
    // getXferStatus on each user call (external-polling path).
    virtual nixl_status_t poll(uint64_t svc_req,
                               std::vector<nixlBlobDesc> &out_descs) = 0;

};

#endif // __SERVICE_ENGINE_H
