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

#ifndef __KVTC_SERVICE_H
#define __KVTC_SERVICE_H

#include <mutex>
#include "service/service_engine.h"

// Forward declaration
class HostClient;

/**
 * @class nixlKvtcServiceEngine
 * @brief KVTC compression/decompression service engine backed by a DPU.
 *
 * Thread-safe and non-blocking; relies on the agent's service PT pool for progress.
 *
 * When postXferReq is called, nixlAgent calls processDataAsync(): DOCA tasks are
 * submitted non-blocking and the request is enqueued with svc_req (= batch_id)
 * and initial out_descs. The agent's shared service progress thread(s) repeatedly
 * call poll(svc_req, out_descs); each call acquires client_mutex_ briefly, ticks
 * the DOCA Progress Engine once, and checks whether the batch is done. On
 * completion, poll() updates out_descs with actual output sizes and returns
 * NIXL_SUCCESS, triggering the agent's fluent backend handoff (postXfer).
 *
 * client_mutex_ serialises the DOCA PE between processDataAsync() (agent thread)
 * and poll() (service progress thread). Contention is bounded to these two
 * threads and the critical section is very short (one DOCA submit or one tick).
 */
class nixlKvtcServiceEngine : public nixlServiceEngine {
public:
    /**
     * @brief Constructor for KVTC service engine.
     * @param init_params Initialization parameters including service type and custom params.
     *                    Required keys: "dev_bdf", "server_name".
     */
    explicit nixlKvtcServiceEngine(const nixlServiceInitParams* init_params);

    /**
     * @brief Destructor.
     */
    ~nixlKvtcServiceEngine() override;

    /**
     * @brief Return worst-case output buffer size.
     * @param input_size  Input data size in bytes.
     * @param op          NIXL_WRITE (compress) or NIXL_READ (decompress).
     * @return Maximum output size in bytes.
     */
    size_t GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const override;

    /**
     * @brief Non-blocking submission.
     *
     * Allocates a batch_id, submits DOCA tasks under client_mutex_, and fills
     * out_descs with pre-allocated output buffer descriptors (worst-case sizes).
     * Returns batch_id as svc_req_out. Returns immediately without waiting for
     * hardware completion.
     */
    nixl_status_t processDataAsync(const nixl_xfer_op_t &operation,
                               const std::vector<nixlBlobDesc> &src_descs,
                               std::vector<nixlBlobDesc> &out_descs,
                               const nixl_s_params_t *service_meta,
                               uint64_t &svc_req_out) override;

    /**
     * @brief Non-blocking progress tick.
     *
     * Acquires client_mutex_ briefly, calls client_->poll() once to tick the
     * DOCA PE, and checks HasPendingTasksForBatch(svc_req). Returns NIXL_IN_PROG
     * while busy. On completion, updates out_descs with actual output sizes (if
     * available from task results) and returns NIXL_SUCCESS. Called by the
     * service handle's progress thread; must not block.
     */
    nixl_status_t poll(uint64_t svc_req,
                       std::vector<nixlBlobDesc> &out_descs) override;

private:
    HostClient* client_;       // HostClient instance for DOCA communication
    std::mutex  client_mutex_; // Serialises DOCA PE between processDataAsync and poll
};

#endif // __KVTC_SERVICE_H
