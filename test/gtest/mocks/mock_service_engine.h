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
#ifndef TEST_GTEST_MOCKS_MOCK_SERVICE_ENGINE_H
#define TEST_GTEST_MOCKS_MOCK_SERVICE_ENGINE_H

#include <gmock/gmock.h>
#include "service/service_engine.h"

namespace mocks {

/**
 * @class GMockServiceEngine
 * @brief A GMock implementation of nixlServiceEngine for GTest testing.
 *
 * For direct use in test scope when call verification on a service engine is needed.
 *
 * Note: nixlServiceH has a private constructor (friend of nixlServiceManager only).
 * To test service handle delegation via nixlServiceManager::createService(), use the
 * MOCK_SERVICE plugin (mock_service_plugin.cpp) which is loaded automatically from
 * the service_pluginlist in debug builds. This GMockServiceEngine is for tests that
 * interact with nixlServiceEngine directly (e.g., testing engine base-class logic).
 *
 * Usage:
 *   nixlServiceInitParams p{"test", nullptr, {{DRAM_SEG}, {DRAM_SEG}}};
 *   testing::NiceMock<mocks::GMockServiceEngine> eng(&p);
 *   EXPECT_CALL(eng, GetMaxBuffersize(1024, NIXL_WRITE)).WillOnce(testing::Return(512));
 */
class GMockServiceEngine : public nixlServiceEngine {
public:
    explicit GMockServiceEngine(const nixlServiceInitParams *init_params)
        : nixlServiceEngine(init_params) {}

    // Public wrappers for protected base-class helpers (for white-box unit testing).
    nixl_status_t
    callSetInitParam(const std::string &key, const std::string &value) {
        return setInitParam(key, value);
    }
    nixl_status_t
    callGetInitParam(const std::string &key, std::string &value) const {
        return getInitParam(key, value);
    }

    MOCK_METHOD(size_t, GetMaxBuffersize, (size_t input_size, nixl_xfer_op_t op),
                (const, override));
    MOCK_METHOD(nixl_status_t,
                processDataAsync,
                (const nixl_xfer_op_t           &op,
                 const std::vector<nixlBlobDesc> &src_descs,
                 std::vector<nixlBlobDesc>       &out_descs,
                 const nixl_s_params_t           *service_meta,
                 uint64_t                        &svc_req_out),
                (override));
    MOCK_METHOD(nixl_status_t,
                poll,
                (uint64_t svc_req, std::vector<nixlBlobDesc> &out_descs),
                (override));
};

} // namespace mocks

#endif // TEST_GTEST_MOCKS_MOCK_SERVICE_ENGINE_H
