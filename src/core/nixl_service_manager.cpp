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

nixl_status_t
nixlServiceManager::getAvailPlugins(std::vector<nixl_service_t> &plugins) {
    auto &pm = nixlPluginManager::getInstance();
    plugins  = pm.getLoadedServicePluginNames();
    return NIXL_SUCCESS;
}

nixl_status_t
nixlServiceManager::getPluginParams(const nixl_service_t &type,
                                    nixl_service_mems_t  &mems,
                                    nixl_s_params_t      &params) {
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

    auto &pm          = nixlPluginManager::getInstance();
    auto  plugin_h    = pm.loadServicePlugin(type);
    if (!plugin_h) {
        NIXL_ERROR << "Failed to load service plugin: " << type;
        return NIXL_ERR_NOT_FOUND;
    }

    nixlServiceInitParams init_params;
    init_params.type         = type;
    init_params.mems         = mems;
    init_params.customParams = &params;

    nixlServiceEngine *engine = plugin_h->createEngine(&init_params);
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
