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

#include "generic_dpu_engine.h"
#include "host_client.h"
#include "nixl_log.h"

#include <chrono>
#include <thread>
#include <iostream>

constexpr auto SELECT_TIMEOUT = std::chrono::milliseconds(30000);
constexpr auto POLL_INTERVAL  = std::chrono::milliseconds(10);

GenericDpuEngine::GenericDpuEngine(const nixlServiceInitParams *init_params)
    : nixlServiceEngine(init_params), hostClient_(nullptr), mutex_(nullptr) {

    std::string hc_ptr_str, mx_ptr_str, service_type;

    if (getInitParam("host_client_ptr", hc_ptr_str) != NIXL_SUCCESS ||
        getInitParam("host_client_mutex_ptr", mx_ptr_str) != NIXL_SUCCESS) {
        NIXL_ERROR << "GenericDpuEngine: missing host_client_ptr / host_client_mutex_ptr";
        initErr = true;
        return;
    }

    hostClient_ = reinterpret_cast<HostClient *>(std::stoull(hc_ptr_str));
    mutex_      = reinterpret_cast<std::mutex *>(std::stoull(mx_ptr_str));

    if (!hostClient_ || !mutex_) {
        NIXL_ERROR << "GenericDpuEngine: null HostClient or mutex pointer";
        initErr = true;
        return;
    }

    if (getInitParam("service_type", service_type) != NIXL_SUCCESS) {
        NIXL_ERROR << "GenericDpuEngine: missing 'service_type' in custom params";
        initErr = true;
        return;
    }

    try {
        std::cout << "HostClient State: " << static_cast<int>(hostClient_->GetState()) << std::endl;
        std::cout << "Selecting service: " << service_type << std::endl;
        hostClient_->SelectService(service_type);

        auto start = std::chrono::steady_clock::now();
        while (hostClient_->GetState() != ClientState::CLIENT_STATE_RUNNING) {
            hostClient_->Poll();
            if (hostClient_->GetState() == ClientState::CLIENT_STATE_FAILED) {
                NIXL_ERROR << "GenericDpuEngine: HostClient entered FAILED state";
                initErr = true;
                return;
            }
            if (std::chrono::steady_clock::now() - start > SELECT_TIMEOUT) {
                NIXL_ERROR << "GenericDpuEngine: timeout waiting for service '"
                           << service_type << "' selection";
                initErr = true;
                return;
            }
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
        NIXL_DEBUG << "GenericDpuEngine: service '" << service_type << "' selected";
    } catch (const std::exception &e) {
        NIXL_ERROR << "GenericDpuEngine: SelectService failed: " << e.what();
        initErr = true;
    }
}

GenericDpuEngine::~GenericDpuEngine() = default;

size_t
GenericDpuEngine::GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const {
    (void)op;
    return input_size;
}

nixl_status_t
GenericDpuEngine::processDataAsync(const nixl_xfer_op_t &operation,
                                   const std::vector<nixlBlobDesc> &src_descs,
                                   std::vector<nixlBlobDesc> &out_descs,
                                   const nixl_s_params_t *service_meta,
                                   uint64_t &svc_req_out) {
    (void)service_meta;

    if (!hostClient_) return NIXL_ERR_BACKEND;

    const uint64_t reqId = nextReqId_++;

    out_descs.clear();
    out_descs.reserve(src_descs.size());
    for (const auto &desc : src_descs)
        out_descs.emplace_back(desc.addr,
                               GetMaxBuffersize(desc.len, operation),
                               desc.devId, nixl_blob_t{});

    std::lock_guard<std::mutex> lk(*mutex_);

    for (size_t i = 0; i < src_descs.size(); ++i) {
        void *src_buf = reinterpret_cast<void *>(src_descs[i].addr);
        void *dst_buf = reinterpret_cast<void *>(out_descs[i].addr);
        try {
            hostClient_->SubmitTask(src_buf, src_descs[i].len,
                                    dst_buf, out_descs[i].len);
            destBufToReqId_[dst_buf] = reqId;
        } catch (const std::exception &e) {
            NIXL_ERROR << "GenericDpuEngine: SubmitTask failed: " << e.what();
            return NIXL_ERR_BACKEND;
        }
    }

    pendingBatches_[reqId] = {static_cast<uint32_t>(src_descs.size()), 0};
    svc_req_out = reqId;

    NIXL_DEBUG << "GenericDpuEngine: batch " << reqId << " submitted ("
               << src_descs.size() << " tasks)";
    return NIXL_SUCCESS;
}

nixl_status_t
GenericDpuEngine::poll(uint64_t svc_req,
                       std::vector<nixlBlobDesc> &out_descs) {
    if (!hostClient_) return NIXL_ERR_BACKEND;

    std::lock_guard<std::mutex> lk(*mutex_);

    auto batch_it = pendingBatches_.find(svc_req);
    if (batch_it == pendingBatches_.end())
        return NIXL_ERR_NOT_FOUND;

    auto resp = hostClient_->Poll();
    if (resp.has_value()) {
        auto dest_it = destBufToReqId_.find(resp->dest_buf);
        if (dest_it != destBufToReqId_.end()) {
            pendingBatches_[dest_it->second].completedTasks++;

            if (dest_it->second == svc_req) {
                for (auto &desc : out_descs) {
                    if (reinterpret_cast<void *>(desc.addr) == resp->dest_buf) {
                        desc.len = resp->result_len;
                        break;
                    }
                }
            }
            destBufToReqId_.erase(dest_it);
        }
    }

    if (batch_it->second.completedTasks >= batch_it->second.totalTasks) {
        pendingBatches_.erase(batch_it);
        NIXL_DEBUG << "GenericDpuEngine: batch " << svc_req << " complete";
        return NIXL_SUCCESS;
    }

    return NIXL_IN_PROG;
}
