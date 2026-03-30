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

#include "nixl_service_manager.h"
#include "plugin_manager.h"
#include "common/nixl_log.h"
#include <chrono>
#include <thread>
#include <iostream>

#if HAVE_DPU_MANAGER
#include "host_client.h"

void
nixlServiceManager::connectDpuManager(const std::string &dev_bdf,
                                      const std::string &server_name) {
    devBdf_     = dev_bdf;
    serverName_ = server_name;
    try {
        HostClientConfig cfg = {dev_bdf, server_name,
                                std::chrono::milliseconds(50000)};
        hostClient_ = std::make_unique<HostClient>(cfg);
        hostClient_->Start();
        NIXL_DEBUG << "HostClient started (bdf=" << dev_bdf
                   << ", server=" << server_name << ")";
        static constexpr auto TIMEOUT       = std::chrono::seconds(10);
        static constexpr auto POLL_INTERVAL = std::chrono::milliseconds(50);
        auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
        while (hostClient_->GetState() != ClientState::CLIENT_STATE_SERVICE_ADVERTISED) {
            hostClient_->Poll();
            if (std::chrono::steady_clock::now() > deadline) {
                NIXL_ERROR << "Timeout waiting for DPU service advertisement"
                           << " (bdf=" << dev_bdf << ", server=" << server_name << ")";
                hostClient_.reset();
                return;
            }
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
    } catch (const std::exception &e) {
        NIXL_ERROR << "Failed to create/start HostClient (bdf=" << dev_bdf
                   << ", server=" << server_name << "): " << e.what();
    }
}
#endif

// Defined here (not in header) so unique_ptr<HostClient> sees the complete type.
nixlServiceManager::nixlServiceManager() = default;
nixlServiceManager::~nixlServiceManager() = default;

nixl_status_t
nixlServiceManager::getAvailPlugins(std::vector<nixl_service_t> &plugins) {
#if HAVE_DPU_MANAGER
    if (hostClient_) {
        std::vector<ServiceDesc> services = hostClient_->QueryServices();
        for (const auto &svc : services)
            plugins.emplace_back(svc.name);
        return NIXL_SUCCESS;
    }
#endif
    auto &pm = nixlPluginManager::getInstance();
    plugins  = pm.getLoadedServicePluginNames();
    return NIXL_SUCCESS;
}

nixl_status_t
nixlServiceManager::getPluginParams(const nixl_service_t &type,
                                    nixl_service_mems_t  &mems,
                                    nixl_s_params_t      &params) {
#if HAVE_DPU_MANAGER
    if (hostClient_)
        return NIXL_SUCCESS;
#endif
    auto &pm     = nixlPluginManager::getInstance();
    auto  handle = pm.loadServicePlugin(type);
    if (!handle) {
        NIXL_ERROR << "Service plugin not found: " << type;
        return NIXL_ERR_NOT_FOUND;
    }
    params       = handle->getServiceOptions();
    mems.input   = handle->getInputMems();
    mems.output  = handle->getOutputMems();
    return NIXL_SUCCESS;
}

nixl_status_t
nixlServiceManager::createService(const nixl_service_t      &type,
                                  const nixl_service_mems_t &mems,
                                  const nixl_s_params_t     &params,
                                  nixlServiceH *&handle) {
    handle = nullptr;

    auto &pm = nixlPluginManager::getInstance();
    nixlServiceInitParams init_params;
    nixlServiceEngine *engine = nullptr;
    std::shared_ptr<const nixlServicePluginHandle> plugin_h;

#if HAVE_DPU_MANAGER
    if (hostClient_) {
        plugin_h = pm.loadServicePlugin("generic_dpu");
        if (!plugin_h) {
            NIXL_ERROR << "Failed to load generic_dpu service plugin";
            return NIXL_ERR_NOT_FOUND;
        }

        nixl_s_params_t dpu_params = params;
        dpu_params["host_client_ptr"]       = std::to_string(reinterpret_cast<uintptr_t>(hostClient_.get()));
        dpu_params["host_client_mutex_ptr"] = std::to_string(reinterpret_cast<uintptr_t>(&hostClientMutex_));

        init_params.type         = type;
        init_params.mems         = mems;
        init_params.customParams = &dpu_params;

        engine = plugin_h->createEngine(&init_params);
    } else
#endif
    {
        plugin_h = pm.loadServicePlugin(type);
        if (!plugin_h) {
            NIXL_ERROR << "Failed to load service plugin: " << type;
            return NIXL_ERR_NOT_FOUND;
        }

        init_params.type         = type;
        init_params.mems         = mems;
        init_params.customParams = &params;

        engine = plugin_h->createEngine(&init_params);
    }

    if (!engine) {
        NIXL_ERROR << "Failed to create service engine for: " << type;
        return NIXL_ERR_BACKEND;
    }

    if (engine->getInitErr()) {
        plugin_h->destroyEngine(engine);
        NIXL_ERROR << "Service engine initialization error for: " << type;
        return NIXL_ERR_BACKEND;
    }

    handle = new nixlServiceH(engine);
    NIXL_DEBUG << "Created service handle for: " << type;
    return NIXL_SUCCESS;
}

void
nixlServiceManager::destroyService(nixlServiceH *&handle) {
    if (!handle) return;

    auto &pm       = nixlPluginManager::getInstance();
    auto  plugin_h = pm.loadServicePlugin(handle->getType());
    if (plugin_h) {
        plugin_h->destroyEngine(handle->engine_);
    } else {
        // Fallback: just delete the engine directly
        delete handle->engine_;
    }
    delete handle;
    handle = nullptr;
    NIXL_DEBUG << "Destroyed service handle";
}
