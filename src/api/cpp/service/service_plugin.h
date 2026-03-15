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

#ifndef __SERVICE_PLUGIN_H
#define __SERVICE_PLUGIN_H

#include "service/service_engine.h"
#include "nixl_log.h"

// Define the service plugin API version
#define NIXL_SERVICE_PLUGIN_API_VERSION 1

// Define the service plugin interface class
class nixlServicePlugin {
public:
    int api_version;

    // Function pointer for creating a new service engine instance
    nixlServiceEngine* (*create_engine)(const nixlServiceInitParams* init_params);

    // Function pointer for destroying a service engine instance
    void (*destroy_engine)(nixlServiceEngine* engine);

    // Function to get the plugin name
    const char* (*get_plugin_name)();

    // Function to get the plugin version
    const char* (*get_plugin_version)();

    // Function to get service options
    nixl_b_params_t (*get_service_options)();

    // Functions to get supported input and output memory types separately
    nixl_mem_list_t (*get_input_mems)();
    nixl_mem_list_t (*get_output_mems)();
};

// Macro to define exported C functions for the service plugin
#define NIXL_SERVICE_PLUGIN_EXPORT __attribute__((visibility("default")))

// Template for creating service plugins with minimal boilerplate
template<typename EngineType> class nixlServicePluginCreator {
public:
    static nixlServicePlugin *
    create(int api_version,
           const char *name,
           const char *version,
           const nixl_b_params_t &params,
           const nixl_mem_list_t &input_mems,
           const nixl_mem_list_t &output_mems) {

        static const char *plugin_name = name;
        static const char *plugin_version = version;
        static const nixl_b_params_t plugin_params = params;
        static const nixl_mem_list_t plugin_input_mems = input_mems;
        static const nixl_mem_list_t plugin_output_mems = output_mems;

        static nixlServicePlugin plugin_instance = {api_version,
                                                    createEngine,
                                                    destroyEngine,
                                                    []() { return plugin_name; },
                                                    []() { return plugin_version; },
                                                    []() { return plugin_params; },
                                                    []() { return plugin_input_mems; },
                                                    []() { return plugin_output_mems; }};

        return &plugin_instance;
    }

private:
    [[nodiscard]] static nixlServiceEngine *
    createEngine(const nixlServiceInitParams *init_params) {
        try {
            return new EngineType(init_params);
        }
        catch (const std::exception &e) {
            NIXL_ERROR << "Failed to create service engine: " << e.what();
            return nullptr;
        }
    }

    static void
    destroyEngine(nixlServiceEngine *engine) {
        delete engine;
    }
};

// Creator Function type for static service plugins
typedef nixlServicePlugin* (*nixlStaticServicePluginCreatorFunc)();

// Plugin must implement these functions for dynamic loading
// Note: extern "C" is required for dynamic loading to avoid C++ name mangling
extern "C" {
// Initialize the service plugin
NIXL_SERVICE_PLUGIN_EXPORT nixlServicePlugin *
nixl_service_plugin_init();

// Cleanup the service plugin
NIXL_SERVICE_PLUGIN_EXPORT void
nixl_service_plugin_fini();
}

#endif // __SERVICE_PLUGIN_H
