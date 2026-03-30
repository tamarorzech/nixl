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

#include "generic_dpu_engine.h"
#include "service/service_plugin.h"

static nixlServicePlugin *plugin_instance = nullptr;

extern "C" {

NIXL_SERVICE_PLUGIN_EXPORT nixlServicePlugin *
nixl_service_plugin_init() {
    if (!plugin_instance) {
        nixl_b_params_t params = {};

        nixl_mem_list_t input_mems  = {DRAM_SEG};
        nixl_mem_list_t output_mems = {DRAM_SEG};

        plugin_instance = nixlServicePluginCreator<GenericDpuEngine>::create(
            NIXL_SERVICE_PLUGIN_API_VERSION,
            "generic_dpu",
            "1.0.0",
            params,
            input_mems,
            output_mems);
    }
    return plugin_instance;
}

NIXL_SERVICE_PLUGIN_EXPORT void
nixl_service_plugin_fini() {
    plugin_instance = nullptr;
}

} // extern "C"

nixlServicePlugin *
createStaticGenericDpuServicePlugin() {
    return nixl_service_plugin_init();
}
