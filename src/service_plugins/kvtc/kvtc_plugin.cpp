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

#include "kvtc_service.h"
#include "service/service_plugin.h"

// Plugin instance
static nixlServicePlugin* plugin_instance = nullptr;

// Plugin initialization function for dynamic loading
extern "C" {

NIXL_SERVICE_PLUGIN_EXPORT nixlServicePlugin*
nixl_service_plugin_init() {
    if (!plugin_instance) {
        // Define plugin parameters
        nixl_b_params_t params = {};

        // Define supported memory types
        nixl_mem_list_t mem_list = {DRAM_SEG};

        // Create the plugin using the template creator
        plugin_instance = nixlServicePluginCreator<nixlKvtcServiceEngine>::create(
            NIXL_SERVICE_PLUGIN_API_VERSION,
            "kvtc",
            "1.0.0",
            params,
            mem_list
        );
    }
    return plugin_instance;
}

NIXL_SERVICE_PLUGIN_EXPORT void
nixl_service_plugin_fini() {
    // Plugin instance is statically allocated by the creator template
    // No cleanup needed
    plugin_instance = nullptr;
}

} // extern "C"

// Static plugin creator function for static linking
nixlServicePlugin* createStaticKVTCServicePlugin() {
    return nixl_service_plugin_init();
}
