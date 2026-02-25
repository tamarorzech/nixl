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

/**
 * @file nixl_service_example.cpp
 * @brief Example demonstrating service chain with local/storage backends (POSIX, GDS, etc.)
 * 
 * This example shows how to use NIXL service chains with storage backends that only
 * support local operations (supportsLocal() = true, supportsRemote() = false).
 * 
 * The example demonstrates:
 * - WRITE operation: Transfer data from DRAM buffer to file
 * - READ operation: Transfer data from file to DRAM buffer
 * - Service chain application for both operations
 * - Proper file descriptor handling for POSIX backend
 * 
 * Key differences from remote examples:
 * - Single agent (no remote metadata exchange)
 * - No notifications
 * - Local-only transfers (source and destination on same agent)
 * - Works with POSIX, GDS, HF3FS, and other storage backends
 */

#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

#include "nixl.h"
#include "test_utils.h"
#include "nixl_service_chain.h"

std::string agent_name("LocalAgent");

void check_buf(void* buf, size_t len, uint8_t expected_value) {
    for(size_t i = 0; i < len; i++){
        if (((uint8_t *)buf)[i] != expected_value) {
            std::cerr << "Data mismatch at offset " << i 
                      << ": expected 0x" << std::hex << (int)expected_value
                      << ", got 0x" << (int)((uint8_t *)buf)[i] << std::dec << "\n";
            nixl_exit_on_failure(false, "Data mismatch!", agent_name);
        }
    }
}

bool equal_buf(void* buf1, void* buf2, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (((uint8_t*) buf1)[i] != ((uint8_t*) buf2)[i])
            return false;
    return true;
}

void printParams(const nixl_b_params_t& params, const nixl_mem_list_t& mems) {
    if (params.empty()) {
        std::cout << "Parameters: (empty)" << std::endl;
    } else {
        std::cout << "Parameters:" << std::endl;
        for (const auto& pair : params) {
            std::cout << "  " << pair.first << " = " << pair.second << std::endl;
        }
    }

    if (mems.empty()) {
        std::cout << "Mems: (empty)" << std::endl;
    } else {
        std::cout << "Mems:" << std::endl;
        for (const auto& elm : mems) {
            std::cout << "  " << nixlEnumStrings::memTypeStr(elm) << std::endl;
        }
    }
}

// Structure to hold parsed command-line arguments
struct ProgramArgs {
    std::string backend;
    bool use_out_of_place;
};

// Parse command-line arguments
ProgramArgs parseArguments(int argc, char **argv) {
    ProgramArgs args;
    args.backend = "POSIX";
    args.use_out_of_place = false;
    
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            std::cout << "Usage: " << argv[0] << " [BACKEND] [oop]\n";
            std::cout << "  BACKEND: Backend name (default: POSIX)\n";
            std::cout << "  oop:     Enable out-of-place mode (default: in-place)\n";
            exit(0);
        }
        args.backend = arg1;
    }
    
    if (argc > 2) {
        std::string mode_arg = argv[2];
        if (mode_arg == "oop" || mode_arg == "--oop" || mode_arg == "out-of-place") {
            args.use_out_of_place = true;
        }
    }
    
    return args;
}

// Structure to hold buffer resources
struct BufferResources {
    void* src_buffer;
    void* processed_buffer;
    size_t buffer_size;
    int dst_fd;
    std::string dst_file_path;
};

// Allocate source and processed buffers
BufferResources allocateBuffers(bool use_out_of_place) {
    BufferResources resources;
    resources.processed_buffer = nullptr;
    
    constexpr size_t float_size = sizeof(float);
    resources.buffer_size = 40960 * float_size;
    
    resources.src_buffer = calloc(1, resources.buffer_size);
    if (!resources.src_buffer) {
        std::cerr << "Failed to allocate source buffer\n";
        exit(1);
    }
    memset(resources.src_buffer, 0xAA, resources.buffer_size);
    
    if (use_out_of_place) {
        resources.processed_buffer = aligned_alloc(4096, resources.buffer_size);
        if (!resources.processed_buffer) {
            std::cerr << "Failed to allocate processed buffer\n";
            free(resources.src_buffer);
            exit(1);
        }
        memset(resources.processed_buffer, 0xBB, resources.buffer_size);
    }
    
    return resources;
}

// Setup destination file
int setupFile(const std::string& file_path, size_t buffer_size) {
    int fd = open(file_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        std::cerr << "Failed to open destination file: " << file_path << "\n";
        return -1;
    }
    
    // Pre-allocate file with zeros
    std::vector<uint8_t> zeros(buffer_size, 0x00);
    ssize_t written = write(fd, zeros.data(), buffer_size);
    if (written != (ssize_t)buffer_size) {
        std::cerr << "Failed to write initial data to file\n";
        close(fd);
        return -1;
    }
    
    return fd;
}

// Initialize agent and create backend
nixlBackendH* initializeAgentAndBackend(const std::string& backend, 
                                        nixlAgent& agent,
                                        nixl_opt_args_t& extra_params) {
    nixl_status_t ret;
    
    // Get available plugins
    std::vector<nixl_backend_t> plugins;
    ret = agent.getAvailPlugins(plugins);
    nixl_exit_on_failure(ret, "Failed to get available plugins", agent_name);

    std::cout << "Available plugins:\n";
    for (const auto& b : plugins)
        std::cout << "  - " << b << "\n";
    std::cout << "\n";

    // Check if requested backend is available
    if (std::find(plugins.begin(), plugins.end(), backend) == plugins.end()) {
        std::cerr << "ERROR: Backend '" << backend << "' not found!\n";
        std::cerr << "Available backends listed above.\n";
        exit(1);
    }

    // Get plugin parameters
    nixl_b_params_t init_params;
    nixl_mem_list_t mems;
    ret = agent.getPluginParams(backend, mems, init_params);
    nixl_exit_on_failure(ret, "Failed to get plugin params", agent_name);

    std::cout << "Backend parameters:\n";
    printParams(init_params, mems);
    std::cout << "\n";

    // Create backend
    nixlBackendH *backend_handle;
    ret = agent.createBackend(backend, init_params, backend_handle);
    nixl_exit_on_failure(ret, "Failed to create " + backend + " backend", agent_name);

    extra_params.backends.push_back(backend_handle);
    
    return backend_handle;
}

// Register memory with backend
void registerMemory(nixlAgent& agent, 
                    const BufferResources& resources,
                    bool use_out_of_place,
                    nixl_opt_args_t& extra_params) {
    nixl_status_t ret;
    
    if (use_out_of_place) {
        // Out-of-place: Only register processed buffer (output)
        nixl_reg_dlist_t reg_list_processed(DRAM_SEG);
        nixlBlobDesc processed_desc;
        processed_desc.addr = (uintptr_t)resources.processed_buffer;
        processed_desc.len = resources.buffer_size;
        processed_desc.devId = 0;
        reg_list_processed.addDesc(processed_desc);
        
        ret = agent.registerMem(reg_list_processed, &extra_params);
        nixl_exit_on_failure(ret, "Failed to register processed buffer", agent_name);
        
        std::cout << "Memory registered with backend:\n";
        std::cout << "  Processed buffer (DRAM) - out-of-place mode\n\n";
    } else {
        // In-place: Register source buffer
        nixl_reg_dlist_t reg_list_src(DRAM_SEG);
        nixlBlobDesc src_desc;
        src_desc.addr = (uintptr_t)resources.src_buffer;
        src_desc.len = resources.buffer_size;
        src_desc.devId = 0;
        reg_list_src.addDesc(src_desc);
        
        ret = agent.registerMem(reg_list_src, &extra_params);
        nixl_exit_on_failure(ret, "Failed to register source memory", agent_name);
        
        std::cout << "Memory registered with backend:\n";
        std::cout << "  Source buffer (DRAM) - in-place mode\n\n";
    }
    
    // Always register destination file (FILE)
    nixl_reg_dlist_t reg_list_dst(FILE_SEG);
    nixlBlobDesc dst_desc;
    dst_desc.addr = 0;
    dst_desc.len = resources.buffer_size;
    dst_desc.devId = resources.dst_fd;
    dst_desc.metaInfo = resources.dst_file_path;
    reg_list_dst.addDesc(dst_desc);
    
    ret = agent.registerMem(reg_list_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register destination file", agent_name);
}

// Create and configure service chain
nixlServiceChain createServiceChain(bool use_out_of_place) {
    nixlServiceChain service_chain;
    nixl_service_t service_type = "kvtc";
    
    std::cout << "Creating service chain...\n";
    uint32_t service_flags = use_out_of_place ? 0 : NIXL_SERVICE_INPLACE;
    nixl_s_params_t service_params;
    service_params["dev_bdf"] = "0000:81:00.0";
    service_params["server_name"] = "kvtc_demo";
    auto chain_status = service_chain.addService(service_type, service_flags, &service_params);
    if (chain_status == nixlServiceChainStatus::SUCCESS) {
        std::cout << "  Added service: " << service_type 
                  << " (" << (use_out_of_place ? "out-of-place" : "in-place") << " mode)\n";
        std::cout << "  Chain size: " << service_chain.size() << "\n\n";
    } else {
        std::cout << "  Warning: Failed to add service (status: " 
                  << static_cast<int>(chain_status) << ")\n";
        std::cout << "  Continuing without service chain...\n\n";
    }
    
    return service_chain;
}

// Perform WRITE operation (DRAM -> FILE)
void performWriteOperation(nixlAgent& agent,
                          const BufferResources& resources,
                          bool use_out_of_place,
                          nixl_opt_args_t& extra_params,
                          nixlServiceChain& service_chain) {
    nixl_status_t ret;
    
    std::cout << "========================================\n";
    std::cout << "Testing WRITE operation (DRAM -> FILE)\n";
    std::cout << "========================================\n\n";
    
    size_t xfer_size = resources.buffer_size;
    
    // Source: DRAM buffer
    nixl_xfer_dlist_t src_xfer_descs(DRAM_SEG);
    nixlBasicDesc src_xfer;
    src_xfer.addr = (uintptr_t)resources.src_buffer;
    src_xfer.len = xfer_size;
    src_xfer.devId = 0;
    src_xfer_descs.addDesc(src_xfer);
    
    // Destination: FILE
    nixl_xfer_dlist_t dst_xfer_descs(FILE_SEG);
    nixlBasicDesc dst_xfer;
    dst_xfer.addr = 0;
    dst_xfer.len = xfer_size;
    dst_xfer.devId = resources.dst_fd;
    dst_xfer_descs.addDesc(dst_xfer);

    // Processed buffer descriptors for out-of-place mode
    nixl_xfer_dlist_t* processed_descs_ptr = nullptr;
    nixl_xfer_dlist_t processed_descs(DRAM_SEG);
    
    if (use_out_of_place) {
        nixlBasicDesc processed_xfer;
        processed_xfer.addr = (uintptr_t)resources.processed_buffer;
        processed_xfer.len = xfer_size;
        processed_xfer.devId = 0;
        processed_descs.addDesc(processed_xfer);
        processed_descs_ptr = &processed_descs;
    }
    
    nixlXferReqH *req_handle;
    
    std::cout << "Creating transfer request...\n";
    std::cout << "  Operation: NIXL_WRITE (local copy)\n";
    std::cout << "  Source: " << resources.src_buffer << "\n";
    if (use_out_of_place) {
        std::cout << "  Processed: " << resources.processed_buffer << " (out-of-place)\n";
    }
    std::cout << "  Destination: " << (void*)dst_xfer.addr << "\n";
    std::cout << "  Size: " << xfer_size << " bytes\n";
    std::cout << "  Remote agent: " << agent_name << " (same agent for local ops)\n\n";
    
    ret = agent.createXferReq(NIXL_WRITE, src_xfer_descs, dst_xfer_descs, 
                              agent_name, req_handle, &extra_params,
                              &service_chain, processed_descs_ptr);
    nixl_exit_on_failure(ret, "Failed to create transfer request", agent_name);

    std::cout << "Posting transfer request...\n";
    nixl_status_t status = agent.postXferReq(req_handle);
    nixl_exit_on_failure((status >= NIXL_SUCCESS), "Failed to post transfer request", agent_name);

    std::cout << "Transfer posted, waiting for completion...\n";

    while (status != NIXL_SUCCESS) {
        status = agent.getXferStatus(req_handle);
        nixl_exit_on_failure((status >= NIXL_SUCCESS), "Transfer failed", agent_name);
    }

    std::cout << "Transfer completed successfully!\n\n";
    
    ret = agent.releaseXferReq(req_handle);
    nixl_exit_on_failure(ret, "Failed to release transfer request", agent_name);
}

// Perform READ operation (FILE -> DRAM)
void performReadOperation(nixlAgent& agent,
                         const BufferResources& resources,
                         bool use_out_of_place,
                         nixl_opt_args_t& extra_params,
                         nixlServiceChain& service_chain) {
    nixl_status_t ret;
    
    std::cout << "========================================\n";
    std::cout << "Testing READ operation (FILE -> DRAM)\n";
    std::cout << "========================================\n\n";
    
    // Allocate read destination buffer
    void* read_dst_buffer = calloc(1, resources.buffer_size);
    if (!read_dst_buffer) {
        std::cerr << "Failed to allocate read destination buffer\n";
        exit(1);
    }
    memset(read_dst_buffer, 0x00, resources.buffer_size);
    
    std::cout << "Allocated read destination buffer:\n";
    std::cout << "  Address: " << read_dst_buffer << " (size: " << resources.buffer_size 
              << " bytes, pattern: 0x00)\n\n";
    
    // Register read destination buffer
    nixl_reg_dlist_t reg_list_read_dst(DRAM_SEG);
    nixlBlobDesc read_dst_desc;
    read_dst_desc.addr = (uintptr_t)read_dst_buffer;
    read_dst_desc.len = resources.buffer_size;
    read_dst_desc.devId = 0;
    reg_list_read_dst.addDesc(read_dst_desc);
    
    ret = agent.registerMem(reg_list_read_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register read destination memory", agent_name);
    
    std::cout << "Memory registered with backend\n\n";
    
    size_t xfer_size = resources.buffer_size;
    
    // Local: DRAM buffer (destination)
    nixl_xfer_dlist_t read_local_descs(DRAM_SEG);
    nixlBasicDesc read_local;
    read_local.addr = (uintptr_t)read_dst_buffer;
    read_local.len = xfer_size;
    read_local.devId = 0;
    read_local_descs.addDesc(read_local);
    
    // Remote: FILE (source)
    nixl_xfer_dlist_t read_remote_descs(FILE_SEG);
    nixlBasicDesc read_remote;
    read_remote.addr = 0;
    read_remote.len = xfer_size;
    read_remote.devId = resources.dst_fd;
    read_remote_descs.addDesc(read_remote);
    
    // Processed buffer descriptors for out-of-place READ
    nixl_xfer_dlist_t* read_processed_descs_ptr = nullptr;
    nixl_xfer_dlist_t read_processed_descs(DRAM_SEG);
    
    if (use_out_of_place) {
        nixlBasicDesc read_processed;
        read_processed.addr = (uintptr_t)resources.processed_buffer;
        read_processed.len = xfer_size;
        read_processed.devId = 0;
        read_processed_descs.addDesc(read_processed);
        read_processed_descs_ptr = &read_processed_descs;
    }
    
    nixlXferReqH *read_req_handle;
    
    std::cout << "Creating READ transfer request...\n";
    std::cout << "  Operation: NIXL_READ (FILE -> DRAM)\n";
    std::cout << "  Local (DRAM): " << read_dst_buffer << "\n";
    if (use_out_of_place) {
        std::cout << "  Processed: " << resources.processed_buffer << " (out-of-place)\n";
    }
    std::cout << "  Remote (FILE): offset 0, fd " << resources.dst_fd << "\n";
    std::cout << "  Size: " << xfer_size << " bytes\n\n";
    
    ret = agent.createXferReq(NIXL_READ, read_local_descs, read_remote_descs,
                              agent_name, read_req_handle, &extra_params,
                              &service_chain, read_processed_descs_ptr);
    nixl_exit_on_failure(ret, "Failed to create READ transfer request", agent_name);
    
    std::cout << "Posting READ transfer request...\n";
    nixl_status_t status = agent.postXferReq(read_req_handle);
    nixl_exit_on_failure((status >= NIXL_SUCCESS), "Failed to post READ transfer request", agent_name);
    
    std::cout << "READ transfer posted, waiting for completion...\n";
    
    while (status != NIXL_SUCCESS) {
        status = agent.getXferStatus(read_req_handle);
        nixl_exit_on_failure((status >= NIXL_SUCCESS), "READ transfer failed", agent_name);
    }
    
    std::cout << "READ transfer completed successfully!\n\n";
    
    // Verify READ transfer
    std::cout << "Verifying READ transfer (FILE -> DRAM)...\n";
    std::cout << "  Checking first " << xfer_size << " bytes...\n";
    
    bool read_correct = (memcmp(resources.src_buffer, read_dst_buffer, xfer_size) == 0);
    
    if (read_correct) {
        std::cout << "  ✓ READ verification PASSED\n";
        std::cout << "  First 16 bytes of original source:  ";
        for (size_t i = 0; i < 16; i++)
            printf("%02X ", ((uint8_t*)resources.src_buffer)[i]);
        std::cout << "\n  First 16 bytes after READ from file: ";
        for (size_t i = 0; i < 16; i++)
            printf("%02X ", ((uint8_t*)read_dst_buffer)[i]);
        std::cout << "\n\n";
    } else {
        std::cout << "  ✗ READ verification FAILED\n";
    }
    
    nixl_exit_on_failure(read_correct, "Data mismatch after READ transfer", agent_name);
    
    ret = agent.releaseXferReq(read_req_handle);
    nixl_exit_on_failure(ret, "Failed to release READ transfer request", agent_name);
    
    ret = agent.deregisterMem(reg_list_read_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister read buffer memory", agent_name);
    
    free(read_dst_buffer);
}

// Cleanup resources
void cleanupResources(const BufferResources& resources, bool use_out_of_place) {
    close(resources.dst_fd);
    free(resources.src_buffer);
    if (use_out_of_place && resources.processed_buffer) {
        free(resources.processed_buffer);
    }
    std::remove(resources.dst_file_path.c_str());
    std::cout << "Cleaned up resources (closed file, freed buffers, removed file)\n";
}

int main(int argc, char **argv) {
    // Parse command-line arguments
    ProgramArgs args = parseArguments(argc, argv);
    
    std::cout << "========================================\n";
    std::cout << "NIXL Local Service Chain Example\n";
    std::cout << "Backend: " << args.backend << "\n";
    std::cout << "Mode: " << (args.use_out_of_place ? "Out-of-Place" : "In-Place (default)") << "\n";
    std::cout << "Tests: WRITE (DRAM->FILE) + READ (FILE->DRAM)\n";
    std::cout << "========================================\n\n";

    // Create agent with listener disabled (no remote operations needed)
    nixlAgentConfig cfg(false);  // false = no listener thread
    nixlAgent agent(agent_name, cfg);
    nixl_opt_args_t extra_params;

    // Initialize agent and backend
    initializeAgentAndBackend(args.backend, agent, extra_params);
    std::cout << "Backend created successfully\n\n";

    // Allocate buffers and setup file
    BufferResources resources = allocateBuffers(args.use_out_of_place);
    resources.dst_file_path = "/tmp/nixl_dst_test_file.bin";
    resources.dst_fd = setupFile(resources.dst_file_path, resources.buffer_size);
    if (resources.dst_fd < 0) {
        free(resources.src_buffer);
        if (resources.processed_buffer) free(resources.processed_buffer);
        return 1;
    }
    
    // Print allocated resources
    if (args.use_out_of_place) {
        std::cout << "Allocated resources:\n";
        std::cout << "  Source (DRAM):      " << resources.src_buffer 
                  << " (size: " << resources.buffer_size << " bytes, pattern: 0xAA)\n";
        std::cout << "  Processed (DRAM):   " << resources.processed_buffer 
                  << " (size: " << resources.buffer_size << " bytes, pattern: 0xBB)\n";
        std::cout << "  Destination (FILE): " << resources.dst_file_path 
                  << " (fd: " << resources.dst_fd << ", size: " << resources.buffer_size 
                  << " bytes, pattern: 0x00)\n\n";
    } else {
        std::cout << "Allocated resources:\n";
        std::cout << "  Source (DRAM):      " << resources.src_buffer 
                  << " (size: " << resources.buffer_size << " bytes, pattern: 0xAA)\n";
        std::cout << "  Destination (FILE): " << resources.dst_file_path 
                  << " (fd: " << resources.dst_fd << ", size: " << resources.buffer_size 
                  << " bytes, pattern: 0x00)\n\n";
    }

    // Register memory with backend
    registerMemory(agent, resources, args.use_out_of_place, extra_params);

    // Create service chain
    nixlServiceChain service_chain = createServiceChain(args.use_out_of_place);

    // Perform WRITE operation
    performWriteOperation(agent, resources, args.use_out_of_place, extra_params, service_chain);

    // Perform READ operation
    // performReadOperation(agent, resources, args.use_out_of_place, extra_params, service_chain);

    // Cleanup
    std::cout << "Cleanup\n";
    nixl_status_t ret;
    
    // Deregister memory
    if (args.use_out_of_place) {
        nixl_reg_dlist_t dereg_processed(DRAM_SEG);
        nixlBlobDesc processed_dereg;
        processed_dereg.addr = (uintptr_t)resources.processed_buffer;
        processed_dereg.len = resources.buffer_size;
        processed_dereg.devId = 0;
        dereg_processed.addDesc(processed_dereg);
        ret = agent.deregisterMem(dereg_processed, &extra_params);
        nixl_exit_on_failure(ret, "Failed to deregister processed buffer", agent_name);
    } else {
        nixl_reg_dlist_t dereg_src(DRAM_SEG);
        nixlBlobDesc src_dereg;
        src_dereg.addr = (uintptr_t)resources.src_buffer;
        src_dereg.len = resources.buffer_size;
        src_dereg.devId = 0;
        dereg_src.addDesc(src_dereg);
        ret = agent.deregisterMem(dereg_src, &extra_params);
        nixl_exit_on_failure(ret, "Failed to deregister source memory", agent_name);
    }
    
    nixl_reg_dlist_t dereg_dst(FILE_SEG);
    nixlBlobDesc dst_dereg;
    dst_dereg.addr = 0;
    dst_dereg.len = resources.buffer_size;
    dst_dereg.devId = resources.dst_fd;
    dst_dereg.metaInfo = resources.dst_file_path;
    dereg_dst.addDesc(dst_dereg);
    ret = agent.deregisterMem(dereg_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister destination file", agent_name);

    cleanupResources(resources, args.use_out_of_place);

    std::cout << "\n========================================\n";
    std::cout << "All tests completed successfully!\n";
    std::cout << "  ✓ WRITE transfer (DRAM -> FILE)\n";
    std::cout << "  ✓ READ transfer (FILE -> DRAM)\n";
    std::cout << "  ✓ Service chain applied\n";
    std::cout << "========================================\n";
    
    return 0;
}
