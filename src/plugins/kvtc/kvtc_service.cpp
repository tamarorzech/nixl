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

nixl_mem_list_t nixlKvtcServiceEngine::getSupportedMems() const {
    // Support all memory types since this is a dummy in-place service
    return {DRAM_SEG, VRAM_SEG, BLK_SEG, OBJ_SEG, FILE_SEG};
}

nixl_status_t nixlKvtcServiceEngine::processData(const nixl_xfer_op_t &operation,
                                                  const std::vector<nixlBlobDesc> &data_descs,
                                                  const std::vector<nixlBlobDesc> &processed_data_descs) {
    // Dummy implementation - does nothing, operates in-place
    (void)operation;  // Suppress unused warning
    (void)data_descs; // Suppress unused warning
    (void)processed_data_descs; // Suppress unused warning

    NIXL_DEBUG << "KVTC service processData called (no-op) for " 
               << data_descs.size() << " descriptor(s)";
    
    if (operation == NIXL_WRITE) {
        try {
            // Prepare source data: random string, size 40960 * sizeof(float)
            // constexpr size_t float_size = sizeof(float);
            for (size_t i = 0; i < data_descs.size(); i++) {
                std::cout << "\nSending compression request:\n";
                std::cout << "  Source size: " << data_descs[i].len << " bytes\n";

                void* dest_buf = nullptr;
                if (processed_data_descs.size() > 0) {
                    dest_buf = reinterpret_cast<void*>(processed_data_descs[i].addr);
                } else {
                    dest_buf = reinterpret_cast<void*>(data_descs[i].addr);
                }
                
                if (client_ == nullptr) {
                    std::cerr << "Error: HostClient not initialized\n";
                    return NIXL_ERR_BACKEND;
                }
                client_->CreateAndSubmitCompSendTask(
                    reinterpret_cast<void*>(data_descs[i].addr), data_descs[i].len, dest_buf, CompType::COMP_TYPE_CAT_X2);

                std::cout << "Request submitted. Waiting for response...\n";

                // Poll for completion (simple demo polling loop)
                auto start_time = std::chrono::steady_clock::now();
                // size_t expected_size = source_size / 2; // CAT_X2 halves the size
                
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                int count = 0;
                while (client_->poll()) {            
                    std::cout << ++count << std::endl;
                    std::this_thread::sleep_for(POLL_INTERVAL);
                    elapsed = std::chrono::steady_clock::now() - start_time;
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                }
                // if (elapsed > TIMEOUT) {
                //     std::cerr << "Timeout reached. Dest buffer contents:\n";
                //     return 1;
                // }
                std::cout << "count = " << count << std::endl;
                std::cout << "Response received. Dest buffer contents:\n";
                std::this_thread::sleep_for(std::chrono::seconds(10));

                float* dest_buf_f = reinterpret_cast<float*>(dest_buf);
                size_t dest_buf_count = data_descs[i].len / sizeof(float);
                for (size_t i = 0; i < std::min(static_cast<size_t>(256), dest_buf_count); ++i) {
                    std::cout << dest_buf_f[i];
                    if ((i + 1) % 8 == 0) {
                        std::cout << "\n";
                    } else {
                        std::cout << "\t";
                    }
                }
                std::cout << "\n";
            }
        }
        catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << "\n";
            return NIXL_ERR_BACKEND;
        }
    }
    return NIXL_SUCCESS;
}
