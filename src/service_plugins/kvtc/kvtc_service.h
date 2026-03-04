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

#include "service/service_engine.h"

// Forward declaration
class HostClient;

/**
 * @class nixlKvtcServiceEngine
 * @brief KVTC service engine - a dummy in-place service that does nothing
 * 
 * This service is a no-op service that operates in-place without modifying data.
 * It can be used as a placeholder or for testing purposes.
 */
class nixlKvtcServiceEngine : public nixlServiceEngine {
public:
    /**
     * @brief Constructor for KVTC service engine
     * @param init_params Initialization parameters including service type and custom params
     */
    explicit nixlKvtcServiceEngine(const nixlServiceInitParams* init_params);

    /**
     * @brief Destructor
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
     * @brief Submit compression/decompression asynchronously.
     *
     * Submits the task to the DPU and returns immediately.
     * Returns NIXL_IN_PROG if the task is in flight, NIXL_SUCCESS if done immediately.
     */
    nixl_status_t processDataAsync(const nixl_xfer_op_t &operation,
                                   const std::vector<nixlBlobDesc> &data_descs) override;

    /**
     * @brief Poll the status of the in-flight DPU task.
     * @return NIXL_IN_PROG while busy, NIXL_SUCCESS when complete.
     */
    nixl_status_t pollProcessData() override;

private:
    HostClient* client_;  // HostClient instance for DOCA communication
};

#endif // __KVTC_SERVICE_H
