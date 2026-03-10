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
 * Implements nixlServiceEngine::processData() as a synchronous, blocking call.
 * The method submits tasks to the DPU via the DOCA CommChannel, then polls the
 * DOCA Progress Engine in a loop (taking and releasing client_mutex_ each
 * iteration) until all tasks in the request complete.
 *
 * client_mutex_ serialises concurrent calls from multiple per-request threads
 * since the DOCA PE is single-threaded. Threads round-robin through the lock,
 * each returning as soon as its own batch is done.
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
     * @brief Perform compression/decompression synchronously.
     *
     * Submits tasks to the DPU and blocks until all tasks for this call are
     * complete. Concurrent calls from different threads are safe: each gets its
     * own batch_id and only returns once its own batch is done.
     *
     * Returns NIXL_SUCCESS when complete, or a negative error code on failure.
     */
    nixl_status_t processData(const nixl_xfer_op_t &operation,
                              const std::vector<nixlBlobDesc> &data_descs) override;

private:
    HostClient* client_;       // HostClient instance for DOCA communication
    std::mutex  client_mutex_; // Serialises poll() calls into the single-threaded DOCA PE
};

#endif // __KVTC_SERVICE_H
