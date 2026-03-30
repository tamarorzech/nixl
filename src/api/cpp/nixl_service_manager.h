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

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nixl_types.h"
#include "nixl_descriptors.h"
#include "service/service_engine.h"

#if HAVE_DPU_MANAGER
class HostClient;
#endif

/**
 * @class nixlServiceH
 * @brief Handle for a service instance created by nixlAgent::addService().
 *        Thin stateless wrapper around a nixlServiceEngine plugin.
 *
 * Owned by the agent; the pointer returned from addService() may be stored by
 * the caller and passed in nixl_opt_args_t::serviceH for transfer requests.
 * The agent is responsible for destroying service handles.
 *
 * Threading model (agent-owned progress threads):
 *   Progress thread policy is set at the agent level via service_enable_pt and
 *   service_progress_threads in nixlAgentConfig. The agent's shared PT pool
 *   drives poll() for all pending service requests across all services and
 *   immediately triggers the backend postXfer() on completion (fluent handoff).
 *   When service_enable_pt is false, getXferStatus() drives poll() inline.
 *
 * The internal methods (processDataAsync, poll) are called by nixlAgent and the
 * agent's service progress threads and are not part of the application-facing API.
 */
class nixlServiceH {
public:
    /**
     * @brief Return the worst-case output buffer size for a given input and operation.
     * @param input_size Size of the input data in bytes.
     * @param op         NIXL_WRITE (encode path) or NIXL_READ (decode path).
     * @return           Maximum number of bytes the service may produce.
     */
    size_t GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const {
        return engine_->GetMaxBuffersize(input_size, op);
    }

    /** @brief Return the service type string. */
    nixl_service_t getType() const { return engine_->getType(); }

    /** @brief Return supported input memory types (used for validation). */
    const nixl_mem_list_t& getSupportedInputMems() const {
        return engine_->getSupportedInputMems();
    }

    /** @brief Return supported output memory types (used for validation). */
    const nixl_mem_list_t& getSupportedOutputMems() const {
        return engine_->getSupportedOutputMems();
    }

    // -- Internal API called by nixlAgent -- //

    /**
     * @brief Non-blocking work submission. Returns a svc_req handle and
     *        pre-allocated output descriptors. Called by postXferReq for both
     *        progress-thread and external-polling paths.
     *
     * @param op           Transfer operation.
     * @param src_descs    Source buffer descriptors.
     * @param out_descs    [out] Pre-allocated output buffer descriptors.
     * @param service_meta Per-request metadata; may be nullptr.
     * @param svc_req_out  [out] Opaque handle for subsequent poll() calls.
     * @return NIXL_SUCCESS if work was submitted; negative on error.
     */
    nixl_status_t processDataAsync(const nixl_xfer_op_t            &op,
                               const std::vector<nixlBlobDesc>  &src_descs,
                               std::vector<nixlBlobDesc>        &out_descs,
                               const nixl_s_params_t            *service_meta,
                               uint64_t                         &svc_req_out) {
        return engine_->processDataAsync(op, src_descs, out_descs, service_meta, svc_req_out);
    }

    /**
     * @brief One non-blocking progress tick for a pending service request.
     *
     * Delegates to engine->poll(). Returns NIXL_IN_PROG while busy,
     * NIXL_SUCCESS when done (engine may update out_descs with actual sizes),
     * or a negative error code on failure.
     *
     * Called by the agent's service progress threads (PT path) or by
     * getXferStatus() once per user poll (external-polling path).
     */
    nixl_status_t poll(uint64_t svc_req, std::vector<nixlBlobDesc> &out_descs) {
        return engine_->poll(svc_req, out_descs);
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
 * @brief Internal factory for service plugin discovery and lifecycle management.
 *
 * This class is an internal implementation detail used by nixlAgent. Applications
 * should use the agent's service API directly:
 *   - nixlAgent::getAvailServicePlugins()
 *   - nixlAgent::getServicePluginParams()
 *   - nixlAgent::addService()
 *
 * The service manager is a pure factory: it creates and destroys nixlServiceH
 * instances that are thin wrappers around the underlying plugin engine.
 */
class nixlServiceManager {
public:
    nixlServiceManager();
    ~nixlServiceManager();

#if HAVE_DPU_MANAGER
    /**
     * @brief Construct and immediately connect a HostClient to the DPU.
     *
     * @param dev_bdf     PCIe BDF of the DPU device (e.g. "03:00.0").
     * @param server_name DOCA Comm Channel server name on the DPU.
     */
    void connectDpuManager(const std::string &dev_bdf,
                           const std::string &server_name);
#endif

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
     * @brief Get the default initialization parameters and supported memory types
     *        for a service plugin.
     * @param type   Service type string (e.g., "kvtc").
     * @param mems   [out] Supported input/output memory types for the service.
     * @param params [out] Map of parameter name → default value.
     * @return NIXL_SUCCESS, or NIXL_ERR_NOT_FOUND if the plugin is unknown.
     */
    nixl_status_t getPluginParams(const nixl_service_t &type,
                                  nixl_service_mems_t  &mems,
                                  nixl_s_params_t      &params);

    /**
     * @brief Instantiate a service engine and return a handle to the caller.
     *
     * The caller owns the returned handle and must call destroyService() when done.
     * The handle must remain valid for the lifetime of all transfer requests that
     * reference it. No progress threads are started here; those are managed by
     * the agent via service_enable_pt / service_progress_threads in nixlAgentConfig.
     *
     * @param type   Service type string.
     * @param mems   Requested input/output memory types (validated against plugin support).
     * @param params Initialization parameters (from getPluginParams, customized).
     * @param handle [out] Pointer to the created service handle.
     * @return NIXL_SUCCESS, or an error code on failure.
     */
    nixl_status_t createService(const nixl_service_t  &type,
                                const nixl_service_mems_t &mems,
                                const nixl_s_params_t &params,
                                nixlServiceH *&handle);

    /**
     * @brief Destroy a service handle previously created by createService().
     * @param handle Handle to destroy. Set to nullptr after destruction.
     */
    void destroyService(nixlServiceH *&handle);

#if HAVE_DPU_MANAGER
    HostClient* getHostClient() { return hostClient_.get(); }
    std::mutex& getHostClientMutex() { return hostClientMutex_; }

private:
    std::unique_ptr<HostClient> hostClient_;
    std::mutex hostClientMutex_;
    std::string devBdf_;
    std::string serverName_;
#endif
};

#endif // _NIXL_SERVICE_MANAGER_H
