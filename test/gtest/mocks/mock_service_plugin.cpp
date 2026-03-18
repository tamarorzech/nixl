/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "service/service_engine.h"
#include "service/service_plugin.h"
#include "common.h"

namespace mocks {
namespace service_plugin {

// Buffer sizing factors used by tests that verify delegation through nixlServiceH.
static constexpr size_t   WRITE_BUFFER_FACTOR = 2;
static constexpr size_t   READ_BUFFER_FACTOR  = 4;
static constexpr uint64_t FIXED_SVC_REQ_ID    = 0xABC;

/**
 * Concrete deterministic service engine used by the MOCK_SERVICE plugin.
 *
 * All virtual method responses are fixed and predictable, which lets unit tests
 * verify that nixlServiceH correctly delegates every call to the underlying engine.
 *
 * Behaviour contract (relied on by service_manager.cpp unit tests):
 *   GetMaxBuffersize(n, NIXL_WRITE)  -> n * WRITE_BUFFER_FACTOR (2)
 *   GetMaxBuffersize(n, NIXL_READ)   -> n * READ_BUFFER_FACTOR  (4)
 *   processDataAsync(...)            -> fills out_descs, svc_req_out = FIXED_SVC_REQ_ID, NIXL_SUCCESS
 *   poll(...)                        -> NIXL_SUCCESS
 */
class MockServiceEngine : public nixlServiceEngine {
public:
    explicit MockServiceEngine(const nixlServiceInitParams *init_params)
        : nixlServiceEngine(init_params) {}

    size_t
    GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const override {
        return (op == NIXL_WRITE) ? input_size * WRITE_BUFFER_FACTOR
                                  : input_size * READ_BUFFER_FACTOR;
    }

    nixl_status_t
    processDataAsync(const nixl_xfer_op_t            &op,
                     const std::vector<nixlBlobDesc>  &src_descs,
                     std::vector<nixlBlobDesc>        &out_descs,
                     const nixl_s_params_t            *,
                     uint64_t                         &svc_req_out) override {
        out_descs.clear();
        for (const auto &desc : src_descs) {
            out_descs.emplace_back(desc.addr, GetMaxBuffersize(desc.len, op), desc.devId);
        }
        svc_req_out = FIXED_SVC_REQ_ID;
        return NIXL_SUCCESS;
    }

    nixl_status_t
    poll(uint64_t, std::vector<nixlBlobDesc> &) override {
        return NIXL_SUCCESS;
    }
};

} // namespace service_plugin
} // namespace mocks

static nixlServicePlugin *plugin_instance = nullptr;

extern "C" {

NIXL_SERVICE_PLUGIN_EXPORT nixlServicePlugin *
nixl_service_plugin_init() {
    if (!plugin_instance) {
        plugin_instance =
            nixlServicePluginCreator<mocks::service_plugin::MockServiceEngine>::create(
                NIXL_SERVICE_PLUGIN_API_VERSION,
                gtest::GetMockServiceName(),
                "0.0.1",
                nixl_b_params_t{},
                nixl_mem_list_t{DRAM_SEG},
                nixl_mem_list_t{DRAM_SEG});
    }
    return plugin_instance;
}

NIXL_SERVICE_PLUGIN_EXPORT void
nixl_service_plugin_fini() {
    plugin_instance = nullptr;
}

} // extern "C"
