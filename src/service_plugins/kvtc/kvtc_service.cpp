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
#include "nixl_log.h"
#include "host/host_client.h"
#include "common/comp_req.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <limits>

constexpr uint32_t NUM_TASKS = 4;
constexpr auto TIMEOUT = std::chrono::milliseconds(5000);
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(10);
constexpr uint32_t KVTC_MAX_BUFFER_DIVISOR = 16;

nixlKvtcServiceEngine::nixlKvtcServiceEngine(const nixlServiceInitParams* init_params)
    : nixlServiceEngine(init_params), client_(nullptr) {
    NIXL_DEBUG << "KVTC service engine created (in-place dummy service)";
    
    std::string dev_bdf;
    std::string server_name;
    // Get parameters from customParams_ (inherited from base class)
    const auto& customParams = getCustomParams();
    auto dev_bdf_iter = customParams.find("dev_bdf");
    if (dev_bdf_iter != customParams.end()) {
        dev_bdf = dev_bdf_iter->second;
    } else {
        throw std::runtime_error("Missing required parameter: dev_bdf");
    }
    auto server_name_iter = customParams.find("server_name");
    if (server_name_iter != customParams.end()) {
        server_name = server_name_iter->second;
    } else {
        throw std::runtime_error("Missing required parameter: server_name");
    }
    
    try {
        std::cout << "Creating HostClient...\n";
        std::cout << "  Device BDF: " << dev_bdf << "\n";
        std::cout << "  Server name: " << server_name << "\n";

        client_ = new HostClient(dev_bdf, server_name, NUM_TASKS, TIMEOUT);

        std::cout << "Starting client and connecting to server...\n";
        client_->start();

        // Wait for handshake to complete
        std::cout << "Waiting for server handshake...\n";
        auto handshake_start = std::chrono::steady_clock::now();
        while (client_->GetState() != HostClient::ClientState::CLIENT_STATE_RUNNING) {
            client_->poll();
            auto elapsed = std::chrono::steady_clock::now() - handshake_start;
            if (elapsed > TIMEOUT) {
                std::cerr << "Timeout waiting for server handshake\n";
                delete client_;
                client_ = nullptr;
                throw std::runtime_error("Timeout waiting for server handshake");
            }
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
        std::cout << "Client connected successfully.\n";
    }
    catch (const std::exception &e) {
        // Clean up if client was partially created
        if (client_ != nullptr) {
            delete client_;
            client_ = nullptr;
        }
        std::cerr << "Error initializing KVTC service: " << e.what() << "\n";
        // Re-throw to indicate construction failure
        throw;
    }
}

nixlKvtcServiceEngine::~nixlKvtcServiceEngine() {
    if (client_ != nullptr) {
        delete client_;
        client_ = nullptr;
    }
}

size_t nixlKvtcServiceEngine::GetMaxBuffersize(size_t input_size, nixl_xfer_op_t op) const {
    // WRITE path (compress): CAT_X2 halves the data, but allocate the full input size
    // as the worst case (incompressible data).
    // READ path (decompress): output can be up to 2x the compressed input size.
    return input_size / KVTC_MAX_BUFFER_DIVISOR; // worst-case: full expansion
}

nixl_status_t nixlKvtcServiceEngine::processDataAsync(const nixl_xfer_op_t &operation,
                                                       const std::vector<nixlBlobDesc> &data_descs) {
    if (client_ == nullptr) {
        NIXL_ERROR << "KVTC: HostClient not initialized";
        return NIXL_ERR_BACKEND;
    }

    if (operation == NIXL_WRITE) {
        // Submit compression tasks for each descriptor; return immediately.
        for (size_t i = 0; i < data_descs.size(); i++) {
            void* dest_buf = reinterpret_cast<void*>(data_descs[i].addr);

            NIXL_DEBUG << "KVTC: submitting compress task, src=" << data_descs[i].addr
                       << " size=" << data_descs[i].len;
            try {
                client_->CreateAndSubmitCompSendTask(
                    reinterpret_cast<void*>(data_descs[i].addr),
                    data_descs[i].len,
                    dest_buf,
                    CompType::COMP_TYPE_KVTC_X16);
            } catch (const std::exception &e) {
                NIXL_ERROR << "KVTC: task submission failed: " << e.what();
                return NIXL_ERR_BACKEND;
            }
        }
    } else {
        // // READ path: decompression — submit tasks similarly.
        // for (size_t i = 0; i < data_descs.size(); i++) {
        //     void* dest_buf = reinterpret_cast<void*>(data_descs[i].addr);

        //     NIXL_DEBUG << "KVTC: submitting decompress task, src=" << data_descs[i].addr
        //                << " size=" << data_descs[i].len;
        //     try {
        //         client_->CreateAndSubmitCompSendTask(
        //             reinterpret_cast<void*>(data_descs[i].addr),
        //             data_descs[i].len,
        //             dest_buf,
        //             CompType::COMP_TYPE_CAT_X2);
        //     } catch (const std::exception &e) {
        //         NIXL_ERROR << "KVTC: task submission failed: " << e.what();
        //         return NIXL_ERR_BACKEND;
        //     }
        // }
    }

    // Tasks submitted asynchronously; caller must poll via pollProcessData().
    return NIXL_IN_PROG;
}

nixl_status_t nixlKvtcServiceEngine::pollProcessData() {
    if (client_ == nullptr) {
        NIXL_ERROR << "KVTC: HostClient not initialized";
        return NIXL_ERR_BACKEND;
    }

    if (client_->poll() || client_->HasPendingTasks()) {
        return NIXL_IN_PROG;
    }

    NIXL_DEBUG << "KVTC: all tasks completed";
    return NIXL_SUCCESS;
}
