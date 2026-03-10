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

#ifndef _NIXL_SERVICE_MANAGER_H
#define _NIXL_SERVICE_MANAGER_H

#include <string>
#include <vector>

#include "nixl_types.h"
#include "nixl_descriptors.h"
#include "service/service_engine.h"

/**
 * @class nixlServiceH
 * @brief User-facing handle for a created service instance.
 *
 * Returned by nixlServiceManager::createService(). Owned by the application;
 * must remain valid for the lifetime of all transfer requests that reference it.
 * The nixlAgent does NOT take ownership of this handle.
 *
 * When a transfer request with a service attached is posted, nixl spawns one
 * dedicated std::thread per request. That thread calls processData() — which
 * blocks until the service work is complete — and then immediately initiates
 * the backend transfer in the same thread. The engine itself has no knowledge
 * of threads or callbacks.
 *
 * The processData() method is called internally by nixlAgent and are not part
 * of the application-facing API.
 */
class nixlServiceH {
public:
    /**
     * @brief Return the worst-case output buffer size for a given input and operation.
     *
     * Call this before createXferReq to size your buffers correctly.
     *
     * @param input_size Size of the input data in bytes.
     * @param op         NIXL_WRITE (encode path) or NIXL_READ (decode path).
     * @return           Maximum number of bytes the service may produce.
     */
    size_t GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const {
        return engine_->GetMaxBuffersize(input_size, op);
    }

    /** @brief Return the service type string. */
    nixl_service_t getType() const { return engine_->getType(); }

    // -- Internal API called by nixlAgent -- //

    /** @brief Perform the data transformation synchronously.
     *
     *         Blocks until the engine has fully completed the operation.
     *         Called from a dedicated per-request thread spawned by nixlAgent;
     *         the caller runs the backend postXfer immediately after this returns. */
    nixl_status_t processData(const nixl_xfer_op_t &op,
                               const std::vector<nixlBlobDesc> &data_descs) {
        return engine_->processData(op, data_descs);
    }

    /** @brief Return supported input memory types (used for validation). */
    const nixl_mem_list_t& getSupportedInputMems() const {
        return engine_->getSupportedInputMems();
    }

    /** @brief Return supported output memory types (used for validation). */
    const nixl_mem_list_t& getSupportedOutputMems() const {
        return engine_->getSupportedOutputMems();
    }

private:
    nixlServiceEngine *engine_;

    // Only nixlServiceManager creates and destroys handles.
    explicit nixlServiceH(nixlServiceEngine *engine) : engine_(engine) {}
    ~nixlServiceH() = default;

    friend class nixlServiceManager;
};

/**
 * @class nixlServiceManager
 * @brief North-bound object for service plugin discovery and lifecycle management.
 *
 * Usage:
 * @code
 *   nixlServiceManager svc_mgr;
 *
 *   std::vector<nixl_service_t> plugins;
 *   svc_mgr.getAvailPlugins(plugins);
 *
 *   nixl_s_params_t params;
 *   svc_mgr.getPluginParams("kvtc", params);
 *   params["dev_bdf"] = "0000:81:00.0";
 *
 *   nixlServiceH *svc_h = nullptr;
 *   svc_mgr.createService("kvtc", params, svc_h);
 *
 *   size_t buf_sz = svc_h->GetMaxBuffersize(input_size, NIXL_WRITE);
 *
 *   agent.createXferReq(NIXL_WRITE, local, remote, name, req, nullptr, svc_h);
 *   // ... post / poll ...
 *
 *   svc_mgr.destroyService(svc_h);
 * @endcode
 */
class nixlServiceManager {
public:
    nixlServiceManager()  = default;
    ~nixlServiceManager() = default;

    // Non-copyable, non-movable
    nixlServiceManager(const nixlServiceManager &)            = delete;
    nixlServiceManager &operator=(const nixlServiceManager &) = delete;
    nixlServiceManager(nixlServiceManager &&)                 = delete;
    nixlServiceManager &operator=(nixlServiceManager &&)      = delete;

    /**
     * @brief List all available service plugin names.
     * @param plugins [out] Vector populated with discovered plugin names.
     * @return NIXL_SUCCESS on success.
     */
    nixl_status_t getAvailPlugins(std::vector<nixl_service_t> &plugins);

    /**
     * @brief Get the default initialization parameters for a service plugin.
     * @param type   Service type string (e.g., "kvtc").
     * @param params [out] Map of parameter name → default value.
     * @return NIXL_SUCCESS, or NIXL_ERR_NOT_FOUND if the plugin is unknown.
     */
    nixl_status_t getPluginParams(const nixl_service_t &type, nixl_s_params_t &params);

    /**
     * @brief Instantiate a service engine and return a handle to the caller.
     *
     * The caller owns the returned handle and must call destroyService() when done.
     * The handle must remain valid for the lifetime of all transfer requests that
     * reference it.
     *
     * @param type   Service type string.
     * @param params Initialization parameters (from getPluginParams, customized).
     * @param handle [out] Pointer to the created service handle.
     * @return NIXL_SUCCESS, or an error code on failure.
     */
    nixl_status_t createService(const nixl_service_t &type,
                                const nixl_s_params_t &params,
                                nixlServiceH *&handle);

    /**
     * @brief Destroy a service handle previously created by createService().
     * @param handle Handle to destroy. Set to nullptr after destruction.
     */
    void destroyService(nixlServiceH *&handle);
};

#endif // _NIXL_SERVICE_MANAGER_H
