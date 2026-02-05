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
     * @param init_params Initialization parameters including service type and flags
     */
    explicit nixlKvtcServiceEngine(const nixlServiceInitParams* init_params);

    /**
     * @brief Destructor
     */
    ~nixlKvtcServiceEngine() override = default;

    /**
     * @brief Get supported memory types for KVTC service
     * @return List of supported memory types (all types)
     */
    nixl_mem_list_t getSupportedMems() const override;

    /**
     * @brief Process data buffers (dummy implementation - does nothing)
     * @param operation Transfer operation type (READ or WRITE)
     * @param data_descs Vector of data buffer descriptors
     * @return Always returns NIXL_SUCCESS
     */
    nixl_status_t processData(const nixl_xfer_op_t &operation,
                              const std::vector<nixlBlobDesc> &data_descs,
                              const std::vector<nixlBlobDesc> &processed_data_descs) override;
};

#endif // __KVTC_SERVICE_H
