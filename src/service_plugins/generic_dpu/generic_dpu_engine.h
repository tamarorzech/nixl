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

#ifndef __GENERIC_DPU_ENGINE_H
#define __GENERIC_DPU_ENGINE_H

#include <atomic>
#include <map>
#include <mutex>
#include "service/service_engine.h"

class HostClient;

/**
 * @class GenericDpuEngine
 * @brief Service engine that offloads work to a DPU via the HostClient transport.
 *
 * On construction the engine calls HostClient::SelectService() for the service
 * name carried in the "service_type" custom parameter and polls until the
 * connection reaches CLIENT_STATE_RUNNING.
 *
 * processDataAsync() submits one HostClient::SubmitTask() per source descriptor.
 * poll() ticks the HostClient PE once and checks whether all tasks for the
 * requested batch have completed.
 *
 * Thread safety: mutex_ serialises SubmitTask / Poll on the shared HostClient
 * between the agent thread (processDataAsync) and the service progress thread
 * (poll).
 */
class GenericDpuEngine : public nixlServiceEngine {
public:
    explicit GenericDpuEngine(const nixlServiceInitParams *init_params);
    ~GenericDpuEngine() override;

    size_t GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const override;

    nixl_status_t processDataAsync(const nixl_xfer_op_t &operation,
                                   const std::vector<nixlBlobDesc> &src_descs,
                                   std::vector<nixlBlobDesc> &out_descs,
                                   const nixl_s_params_t *service_meta,
                                   uint64_t &svc_req_out) override;

    nixl_status_t poll(uint64_t svc_req,
                       std::vector<nixlBlobDesc> &out_descs) override;

private:
    HostClient *hostClient_;
    std::mutex *mutex_;
    std::atomic<uint64_t> nextReqId_{1};

    struct PendingBatch {
        uint32_t totalTasks;
        uint32_t completedTasks;
    };

    std::map<uint64_t, PendingBatch> pendingBatches_;
    std::map<void *, uint64_t>       destBufToReqId_;
};

#endif // __GENERIC_DPU_ENGINE_H
