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

// mock_service_engine.h must be included at file scope (before any namespace
// declarations) because GMock's MOCK_METHOD macros use ::testing:: absolute
// lookups that break when expanded inside a nested namespace.
#include "mocks/mock_service_engine.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "common.h"
#include "nixl_service_manager.h"
#include "nixl_descriptors.h"
#include "nixl_types.h"

// Buffer sizing constants that match mock_service_plugin.cpp's MockServiceEngine.
static constexpr size_t MOCK_WRITE_BUFFER_FACTOR = 2;
static constexpr size_t MOCK_READ_BUFFER_FACTOR  = 4;

// ──────────────────────────────────────────────────────────────────────────────
// ServiceManagerFixture – tests nixlServiceManager and nixlServiceH delegation
// ──────────────────────────────────────────────────────────────────────────────

namespace gtest {
namespace service {

class ServiceManagerFixture : public testing::Test {
protected:
    nixlServiceManager mgr_;

    nixlServiceH *
    createMockHandle() {
        nixlServiceH       *handle = nullptr;
        nixl_service_mems_t mems{{DRAM_SEG}, {DRAM_SEG}};
        nixl_s_params_t     params;
        EXPECT_EQ(mgr_.createService(GetMockServiceName(), mems, params, handle), NIXL_SUCCESS)
            << "createService failed; is MOCK_SERVICE in the service_pluginlist?";
        if (!handle)
            return nullptr;
        return handle;
    }
};

// ── getAvailPlugins ──────────────────────────────────────────────────────────

TEST_F(ServiceManagerFixture, GetAvailPluginsSucceeds) {
    std::vector<nixl_service_t> plugins;
    EXPECT_EQ(mgr_.getAvailPlugins(plugins), NIXL_SUCCESS);
}

TEST_F(ServiceManagerFixture, GetAvailPluginsContainsMockService) {
    std::vector<nixl_service_t> plugins;
    ASSERT_EQ(mgr_.getAvailPlugins(plugins), NIXL_SUCCESS);
    EXPECT_NE(std::find(plugins.begin(), plugins.end(), GetMockServiceName()), plugins.end());
}

// ── getPluginParams ──────────────────────────────────────────────────────────

TEST_F(ServiceManagerFixture, GetPluginParamsUnknownReturnsError) {
    nixl_service_mems_t mems;
    nixl_s_params_t     params;
    EXPECT_NE(mgr_.getPluginParams("NO_SUCH_SERVICE", mems, params), NIXL_SUCCESS);
}

TEST_F(ServiceManagerFixture, GetPluginParamsMockServiceSucceeds) {
    nixl_service_mems_t mems;
    nixl_s_params_t     params;
    ASSERT_EQ(mgr_.getPluginParams(GetMockServiceName(), mems, params), NIXL_SUCCESS);
    EXPECT_FALSE(mems.input.empty());
    EXPECT_FALSE(mems.output.empty());
}

TEST_F(ServiceManagerFixture, GetPluginParamsMockServiceInputMemContainsDram) {
    nixl_service_mems_t mems;
    nixl_s_params_t     params;
    ASSERT_EQ(mgr_.getPluginParams(GetMockServiceName(), mems, params), NIXL_SUCCESS);
    EXPECT_NE(std::find(mems.input.begin(), mems.input.end(), DRAM_SEG), mems.input.end());
}

TEST_F(ServiceManagerFixture, GetPluginParamsMockServiceOutputMemContainsDram) {
    nixl_service_mems_t mems;
    nixl_s_params_t     params;
    ASSERT_EQ(mgr_.getPluginParams(GetMockServiceName(), mems, params), NIXL_SUCCESS);
    EXPECT_NE(std::find(mems.output.begin(), mems.output.end(), DRAM_SEG), mems.output.end());
}

// ── createService / destroyService ──────────────────────────────────────────

TEST_F(ServiceManagerFixture, CreateServiceUnknownReturnsError) {
    nixlServiceH       *handle = nullptr;
    nixl_service_mems_t mems;
    nixl_s_params_t     params;
    EXPECT_NE(mgr_.createService("NO_SUCH_SERVICE", mems, params, handle), NIXL_SUCCESS);
    EXPECT_EQ(handle, nullptr);
}

TEST_F(ServiceManagerFixture, DestroyServiceNullsHandle) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);
    mgr_.destroyService(handle);
    EXPECT_EQ(handle, nullptr);
}

// ── nixlServiceH delegation ──────────────────────────────────────────────────

TEST_F(ServiceManagerFixture, ServiceHandleGetType) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->getType(), std::string(GetMockServiceName()));
    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleGetSupportedInputMems) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);
    const auto &mems = handle->getSupportedInputMems();
    EXPECT_FALSE(mems.empty());
    EXPECT_NE(std::find(mems.begin(), mems.end(), DRAM_SEG), mems.end());
    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleGetSupportedOutputMems) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);
    const auto &mems = handle->getSupportedOutputMems();
    EXPECT_FALSE(mems.empty());
    EXPECT_NE(std::find(mems.begin(), mems.end(), DRAM_SEG), mems.end());
    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleGetMaxBuffersizeWrite) {
    nixlServiceH    *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);
    constexpr size_t input = 1024;
    EXPECT_EQ(handle->GetMaxBuffersize(input, NIXL_WRITE), input * MOCK_WRITE_BUFFER_FACTOR);
    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleGetMaxBuffersizeRead) {
    nixlServiceH    *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);
    constexpr size_t input = 1024;
    EXPECT_EQ(handle->GetMaxBuffersize(input, NIXL_READ), input * MOCK_READ_BUFFER_FACTOR);
    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleProcessDataAsyncWrite) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);

    static char buf[512];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    EXPECT_EQ(handle->processDataAsync(NIXL_WRITE, src, out, nullptr, svc_req), NIXL_SUCCESS);
    ASSERT_EQ(out.size(), src.size());
    EXPECT_EQ(out[0].len, src[0].len * MOCK_WRITE_BUFFER_FACTOR);
    EXPECT_NE(svc_req, 0u);

    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleProcessDataAsyncRead) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);

    static char buf[512];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    EXPECT_EQ(handle->processDataAsync(NIXL_READ, src, out, nullptr, svc_req), NIXL_SUCCESS);
    ASSERT_EQ(out.size(), src.size());
    EXPECT_EQ(out[0].len, src[0].len * MOCK_READ_BUFFER_FACTOR);

    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandlePollReturnsSuccess) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);

    static char buf[512];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    ASSERT_EQ(handle->processDataAsync(NIXL_WRITE, src, out, nullptr, svc_req), NIXL_SUCCESS);
    EXPECT_EQ(handle->poll(svc_req, out), NIXL_SUCCESS);

    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleProcessDataAsyncEmptySrc) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);

    std::vector<nixlBlobDesc> src;
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    EXPECT_EQ(handle->processDataAsync(NIXL_WRITE, src, out, nullptr, svc_req), NIXL_SUCCESS);
    EXPECT_TRUE(out.empty());

    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, MultipleHandlesIndependent) {
    nixlServiceH *h1 = createMockHandle();
    nixlServiceH *h2 = createMockHandle();
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h2, nullptr);
    EXPECT_NE(h1, h2);

    static char buf1[256], buf2[512];
    std::vector<nixlBlobDesc> src1{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf1), sizeof(buf1), 0)};
    std::vector<nixlBlobDesc> src2{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf2), sizeof(buf2), 0)};
    std::vector<nixlBlobDesc> out1, out2;
    uint64_t req1 = 0, req2 = 0;

    ASSERT_EQ(h1->processDataAsync(NIXL_WRITE, src1, out1, nullptr, req1), NIXL_SUCCESS);
    ASSERT_EQ(h2->processDataAsync(NIXL_READ, src2, out2, nullptr, req2), NIXL_SUCCESS);

    ASSERT_EQ(out1.size(), 1u);
    ASSERT_EQ(out2.size(), 1u);
    EXPECT_EQ(out1[0].len, sizeof(buf1) * MOCK_WRITE_BUFFER_FACTOR);
    EXPECT_EQ(out2[0].len, sizeof(buf2) * MOCK_READ_BUFFER_FACTOR);

    mgr_.destroyService(h1);

    EXPECT_EQ(h2->poll(req2, out2), NIXL_SUCCESS);

    mgr_.destroyService(h2);
}

// ── Service metadata ─────────────────────────────────────────────────────────

TEST_F(ServiceManagerFixture, ServiceHandleProcessDataAsyncPassesNullMetadata) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);

    static char buf[256];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    EXPECT_EQ(handle->processDataAsync(NIXL_WRITE, src, out, nullptr, svc_req), NIXL_SUCCESS);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].len, sizeof(buf) * MOCK_WRITE_BUFFER_FACTOR);

    mgr_.destroyService(handle);
}

TEST_F(ServiceManagerFixture, ServiceHandleProcessDataAsyncPassesMetadata) {
    nixlServiceH *handle = createMockHandle();
    ASSERT_NE(handle, nullptr);

    static char buf[256];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;
    nixl_s_params_t meta{{"compression", "lz4"}, {"level", "3"}};

    EXPECT_EQ(handle->processDataAsync(NIXL_WRITE, src, out, &meta, svc_req), NIXL_SUCCESS);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].len, sizeof(buf) * MOCK_WRITE_BUFFER_FACTOR);

    mgr_.destroyService(handle);
}

} // namespace service
} // namespace gtest

// ──────────────────────────────────────────────────────────────────────────────
// ServiceEngineBaseClass – tests nixlServiceEngine base-class helpers directly.
//
// These tests live outside namespace gtest::service so that MOCK_METHOD macros
// in GMockServiceEngine (already expanded at file scope above) resolve
// ::testing:: correctly.
// ──────────────────────────────────────────────────────────────────────────────

namespace gtest {

static nixlServiceInitParams
makeInitParams(const char *type, nixl_s_params_t *custom = nullptr) {
    nixl_service_mems_t mems{{DRAM_SEG}, {DRAM_SEG}};
    return nixlServiceInitParams{type, custom, mems};
}

TEST(ServiceEngineBaseClass, SetAndGetInitParam) {
    nixl_s_params_t   custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    EXPECT_EQ(eng.callSetInitParam("key1", "val1"), NIXL_SUCCESS);
    EXPECT_EQ(eng.callSetInitParam("key1", "val2"), NIXL_ERR_NOT_ALLOWED);

    std::string out;
    EXPECT_EQ(eng.callGetInitParam("key1", out), NIXL_SUCCESS);
    EXPECT_EQ(out, "val1");
}

TEST(ServiceEngineBaseClass, GetInitParamMissingKey) {
    nixl_s_params_t   custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    std::string out;
    EXPECT_EQ(eng.callGetInitParam("nonexistent", out), NIXL_ERR_INVALID_PARAM);
}

TEST(ServiceEngineBaseClass, GetType) {
    nixl_s_params_t   custom;
    nixlServiceInitParams p = makeInitParams("myservice", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);
    EXPECT_EQ(eng.getType(), std::string("myservice"));
}

TEST(ServiceEngineBaseClass, GetInitErrDefaultFalse) {
    nixl_s_params_t   custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);
    EXPECT_FALSE(eng.getInitErr());
}

TEST(ServiceEngineBaseClass, CustomParamsPropagated) {
    nixl_s_params_t   custom{{"alpha", "1"}, {"beta", "2"}};
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    std::string v;
    EXPECT_EQ(eng.callGetInitParam("alpha", v), NIXL_SUCCESS);
    EXPECT_EQ(v, "1");
    EXPECT_EQ(eng.callGetInitParam("beta", v), NIXL_SUCCESS);
    EXPECT_EQ(v, "2");
}

TEST(ServiceEngineBaseClass, NullCustomParamsSafe) {
    nixlServiceInitParams p = makeInitParams("test");
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    std::string v;
    EXPECT_EQ(eng.callGetInitParam("anything", v), NIXL_ERR_INVALID_PARAM);
}

TEST(ServiceEngineBaseClass, GetCustomParamsReflectsInitParams) {
    nixl_s_params_t   custom{{"x", "42"}};
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    const auto &stored = eng.getCustomParams();
    auto it = stored.find("x");
    ASSERT_NE(it, stored.end());
    EXPECT_EQ(it->second, "42");
}

TEST(ServiceEngineBaseClass, GetSupportedInputMems) {
    nixl_s_params_t   custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    const auto &mems = eng.getSupportedInputMems();
    EXPECT_FALSE(mems.empty());
    EXPECT_NE(std::find(mems.begin(), mems.end(), DRAM_SEG), mems.end());
}

TEST(ServiceEngineBaseClass, GetSupportedOutputMems) {
    nixl_s_params_t   custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    const auto &mems = eng.getSupportedOutputMems();
    EXPECT_FALSE(mems.empty());
    EXPECT_NE(std::find(mems.begin(), mems.end(), DRAM_SEG), mems.end());
}

// ── GMock-based metadata forwarding verification ─────────────────────────────

TEST(ServiceEngineBaseClass, ProcessDataAsyncReceivesMetadataPointer) {
    nixl_s_params_t custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    nixl_s_params_t meta{{"compression", "zstd"}, {"level", "5"}};

    EXPECT_CALL(eng, processDataAsync(
        testing::_, testing::_, testing::_, &meta, testing::_))
        .WillOnce([&](const nixl_xfer_op_t &,
                      const std::vector<nixlBlobDesc> &,
                      std::vector<nixlBlobDesc> &,
                      const nixl_s_params_t *received_meta,
                      uint64_t &svc_req_out) -> nixl_status_t {
            EXPECT_NE(received_meta, nullptr);
            EXPECT_EQ(received_meta->at("compression"), "zstd");
            EXPECT_EQ(received_meta->at("level"), "5");
            svc_req_out = 42;
            return NIXL_SUCCESS;
        });

    static char buf[128];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    EXPECT_EQ(eng.processDataAsync(NIXL_WRITE, src, out, &meta, svc_req), NIXL_SUCCESS);
    EXPECT_EQ(svc_req, 42u);
}

TEST(ServiceEngineBaseClass, ProcessDataAsyncReceivesNullMetadata) {
    nixl_s_params_t custom;
    nixlServiceInitParams p = makeInitParams("test", &custom);
    testing::NiceMock<mocks::GMockServiceEngine> eng(&p);

    EXPECT_CALL(eng, processDataAsync(
        testing::_, testing::_, testing::_,
        testing::IsNull(), testing::_))
        .WillOnce([](const nixl_xfer_op_t &,
                     const std::vector<nixlBlobDesc> &,
                     std::vector<nixlBlobDesc> &,
                     const nixl_s_params_t *received_meta,
                     uint64_t &svc_req_out) -> nixl_status_t {
            EXPECT_EQ(received_meta, nullptr);
            svc_req_out = 0;
            return NIXL_SUCCESS;
        });

    static char buf[128];
    std::vector<nixlBlobDesc> src{nixlBlobDesc(reinterpret_cast<uintptr_t>(buf), sizeof(buf), 0)};
    std::vector<nixlBlobDesc> out;
    uint64_t svc_req = 0;

    EXPECT_EQ(eng.processDataAsync(NIXL_WRITE, src, out, nullptr, svc_req), NIXL_SUCCESS);
}

} // namespace gtest
