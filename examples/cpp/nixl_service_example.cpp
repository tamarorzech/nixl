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

int main(int argc, char **argv) {
    nixl_status_t ret;
    
    // Parse command-line arguments
    // Usage: nixl_service_example [BACKEND] [oop]
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            std::cout << "Usage: " << argv[0] << " [BACKEND] [oop]\n";
            std::cout << "  BACKEND: Backend name (default: POSIX)\n";
            std::cout << "  oop:     Enable out-of-place mode (default: in-place)\n";
            return 0;
        }
    }
    
    std::string backend = "POSIX";
    bool use_out_of_place = false;  // Default: in-place mode
    
    if (argc > 1) {
        backend = argv[1];
    }
    
    if (argc > 2) {
        std::string mode_arg = argv[2];
        if (mode_arg == "oop" || mode_arg == "--oop" || mode_arg == "out-of-place") {
            use_out_of_place = true;
        }
    }

    std::cout << "========================================\n";
    std::cout << "NIXL Local Service Chain Example\n";
    std::cout << "Backend: " << backend << "\n";
    std::cout << "Mode: " << (use_out_of_place ? "Out-of-Place" : "In-Place (default)") << "\n";
    std::cout << "Tests: WRITE (DRAM->FILE) + READ (FILE->DRAM)\n";
    std::cout << "========================================\n\n";

    // Create agent with listener disabled (no remote operations needed)
    nixlAgentConfig cfg(false);  // false = no listener thread
    nixlAgent agent(agent_name, cfg);

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
        return 1;
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

    nixl_opt_args_t extra_params;
    extra_params.backends.push_back(backend_handle);

    std::cout << "Backend created successfully\n\n";

    // POSIX backend: Local=DRAM, Remote=FILE
    // Allocate DRAM buffer (source)
    size_t buffer_size = 1024;
    void* src_buffer = calloc(1, buffer_size);
    if (!src_buffer) {
        std::cerr << "Failed to allocate source buffer\n";
        return 1;
    }
    memset(src_buffer, 0xAA, buffer_size);
    
    // Create and open destination file
    std::string dst_file_path = "/tmp/nixl_dst_test_file.bin";
    
    // Allocate processed buffer for out-of-place mode (before opening file)
    void* processed_buffer = nullptr;
    if (use_out_of_place) {
        processed_buffer = aligned_alloc(4096, buffer_size);
        if (!processed_buffer) {
            std::cerr << "Failed to allocate processed buffer\n";
            free(src_buffer);
            return 1;
        }
        memset(processed_buffer, 0xBB, buffer_size);  // Initialize with different pattern
    }
    
    // Open file with O_CREAT to create if doesn't exist, O_RDWR for read/write
    int dst_fd = open(dst_file_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (dst_fd < 0) {
        std::cerr << "Failed to open destination file: " << dst_file_path << "\n";
        free(src_buffer);
        if (processed_buffer) free(processed_buffer);
        return 1;
    }
    
    // Pre-allocate file with zeros using write
    std::vector<uint8_t> zeros(buffer_size, 0x00);
    ssize_t written = write(dst_fd, zeros.data(), buffer_size);
    if (written != (ssize_t)buffer_size) {
        std::cerr << "Failed to write initial data to file\n";
        close(dst_fd);
        free(src_buffer);
        if (processed_buffer) free(processed_buffer);
        return 1;
    }
    
    if (use_out_of_place) {
        std::cout << "Allocated resources:\n";
        std::cout << "  Source (DRAM):      " << src_buffer << " (size: " << buffer_size << " bytes, pattern: 0xAA)\n";
        std::cout << "  Processed (DRAM):   " << processed_buffer << " (size: " << buffer_size << " bytes, pattern: 0xBB)\n";
        std::cout << "  Destination (FILE): " << dst_file_path << " (fd: " << dst_fd << ", size: " << buffer_size << " bytes, pattern: 0x00)\n\n";
    } else {
        std::cout << "Allocated resources:\n";
        std::cout << "  Source (DRAM):      " << src_buffer << " (size: " << buffer_size << " bytes, pattern: 0xAA)\n";
        std::cout << "  Destination (FILE): " << dst_file_path << " (fd: " << dst_fd << ", size: " << buffer_size << " bytes, pattern: 0x00)\n\n";
    }

    // Register memory based on mode
    if (use_out_of_place) {
        // Out-of-place: Only register processed buffer (output)
        nixl_reg_dlist_t reg_list_processed(DRAM_SEG);
        nixlBlobDesc processed_desc;
        processed_desc.addr = (uintptr_t)processed_buffer;
        processed_desc.len = buffer_size;
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
        src_desc.addr = (uintptr_t)src_buffer;
        src_desc.len = buffer_size;
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
    dst_desc.addr = 0;  // Not used for registration
    dst_desc.len = buffer_size;
    dst_desc.devId = dst_fd;  // File descriptor!
    dst_desc.metaInfo = dst_file_path;  // File path for query operations
    reg_list_dst.addDesc(dst_desc);
    
    ret = agent.registerMem(reg_list_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register destination file", agent_name);

    // Create service chain (will be applied for storage backends only)
    nixlServiceChain service_chain;
    nixl_service_t service_type = "kvtc";  // KVTC is a no-op service for testing
    
    std::cout << "Creating service chain...\n";
    // Set flags based on command-line argument (default: in-place)
    uint32_t service_flags = use_out_of_place ? 0 : NIXL_SERVICE_INPLACE;
    auto chain_status = service_chain.addService(service_type, service_flags);
    if (chain_status == nixlServiceChainStatus::SUCCESS) {
        std::cout << "  Added service: " << service_type 
                  << " (" << (use_out_of_place ? "out-of-place" : "in-place") << " mode)\n";
        std::cout << "  Chain size: " << service_chain.size() << "\n\n";
    } else {
        std::cout << "  Warning: Failed to add service (status: " 
                  << static_cast<int>(chain_status) << ")\n";
        std::cout << "  Continuing without service chain...\n\n";
    }

    // ========================================
    // TEST WRITE OPERATION (DRAM -> FILE)
    // ========================================
    
    std::cout << "========================================\n";
    std::cout << "Testing WRITE operation (DRAM -> FILE)\n";
    std::cout << "========================================\n\n";
    
    // Create transfer descriptors
    size_t xfer_size = 256;  // Transfer 256 bytes
    
    // Source: DRAM buffer
    nixl_xfer_dlist_t src_xfer_descs(DRAM_SEG);
    nixlBasicDesc src_xfer;
    src_xfer.addr = (uintptr_t)src_buffer;
    src_xfer.len = xfer_size;
    src_xfer.devId = 0;
    src_xfer_descs.addDesc(src_xfer);
    
    // Destination: FILE (offset in file)
    nixl_xfer_dlist_t dst_xfer_descs(FILE_SEG);
    nixlBasicDesc dst_xfer;
    dst_xfer.addr = 0;  // Offset in file (start of file)
    dst_xfer.len = xfer_size;
    dst_xfer.devId = dst_fd;  // File descriptor
    dst_xfer_descs.addDesc(dst_xfer);

    // Create processed buffer descriptors for out-of-place mode
    nixl_xfer_dlist_t* processed_descs_ptr = nullptr;
    nixl_xfer_dlist_t processed_descs(DRAM_SEG);
    
    if (use_out_of_place) {
        nixlBasicDesc processed_xfer;
        processed_xfer.addr = (uintptr_t)processed_buffer;
        processed_xfer.len = xfer_size;
        processed_xfer.devId = 0;
        processed_descs.addDesc(processed_xfer);
        processed_descs_ptr = &processed_descs;
    }
    
    // Create transfer request (local operation)
    nixlXferReqH *req_handle;
    
    std::cout << "Creating transfer request...\n";
    std::cout << "  Operation: NIXL_WRITE (local copy)\n";
    std::cout << "  Source: " << (void*)src_xfer.addr << "\n";
    if (use_out_of_place) {
        std::cout << "  Processed: " << processed_buffer << " (out-of-place)\n";
    }
    std::cout << "  Destination: " << (void*)dst_xfer.addr << "\n";
    std::cout << "  Size: " << xfer_size << " bytes\n";
    std::cout << "  Remote agent: " << agent_name << " (same agent for local ops)\n\n";
    
    // For local backends, remote_agent should be the same agent
    ret = agent.createXferReq(NIXL_WRITE, src_xfer_descs, dst_xfer_descs, 
                              agent_name,  // Same agent for local operations
                              &service_chain,
                              processed_descs_ptr,
                              req_handle, 
                              &extra_params);
    nixl_exit_on_failure(ret, "Failed to create transfer request", agent_name);

    // Post transfer request
    std::cout << "Posting transfer request...\n";
    nixl_status_t status = agent.postXferReq(req_handle);
    nixl_exit_on_failure((status >= NIXL_SUCCESS), "Failed to post transfer request", agent_name);

    std::cout << "Transfer posted, waiting for completion...\n";

    // Wait for transfer completion
    while (status != NIXL_SUCCESS) {
        status = agent.getXferStatus(req_handle);
        nixl_exit_on_failure((status >= NIXL_SUCCESS), "Transfer failed", agent_name);
    }

    std::cout << "Transfer completed successfully!\n\n";

    // ========================================
    // TEST READ OPERATION (FILE -> DRAM)
    // ========================================
    
    std::cout << "========================================\n";
    std::cout << "Testing READ operation (FILE -> DRAM)\n";
    std::cout << "========================================\n\n";
    
    // Allocate a new destination buffer for read operation
    void* read_dst_buffer = calloc(1, buffer_size);
    if (!read_dst_buffer) {
        std::cerr << "Failed to allocate read destination buffer\n";
        return 1;
    }
    memset(read_dst_buffer, 0x00, buffer_size);  // Initialize with zeros
    
    std::cout << "Allocated read destination buffer:\n";
    std::cout << "  Address: " << read_dst_buffer << " (size: " << buffer_size << " bytes, pattern: 0x00)\n\n";
    
    // Register the new destination buffer
    nixl_reg_dlist_t reg_list_read_dst(DRAM_SEG);
    nixlBlobDesc read_dst_desc;
    read_dst_desc.addr = (uintptr_t)read_dst_buffer;
    read_dst_desc.len = buffer_size;
    read_dst_desc.devId = 0;
    reg_list_read_dst.addDesc(read_dst_desc);
    
    ret = agent.registerMem(reg_list_read_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register read destination memory", agent_name);
    
    std::cout << "Memory registered with backend\n\n";
    
    // Create READ transfer descriptors
    // For NIXL_READ with POSIX:
    //   - local (first param) = DRAM buffer (where we read INTO)
    //   - remote (second param) = FILE (where we read FROM)
    
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
    read_remote.addr = 0;  // Offset in file (start)
    read_remote.len = xfer_size;
    read_remote.devId = dst_fd;  // Same file descriptor
    read_remote_descs.addDesc(read_remote);
    
    // Create processed buffer descriptors for out-of-place READ
    nixl_xfer_dlist_t* read_processed_descs_ptr = nullptr;
    nixl_xfer_dlist_t read_processed_descs(DRAM_SEG);
    
    if (use_out_of_place) {
        nixlBasicDesc read_processed;
        read_processed.addr = (uintptr_t)processed_buffer;
        read_processed.len = xfer_size;
        read_processed.devId = 0;
        read_processed_descs.addDesc(read_processed);
        read_processed_descs_ptr = &read_processed_descs;
    }
    
    // Create READ transfer request
    nixlXferReqH *read_req_handle;
    
    std::cout << "Creating READ transfer request...\n";
    std::cout << "  Operation: NIXL_READ (FILE -> DRAM)\n";
    std::cout << "  Local (DRAM): " << read_dst_buffer << "\n";
    if (use_out_of_place) {
        std::cout << "  Processed: " << processed_buffer << " (out-of-place)\n";
    }
    std::cout << "  Remote (FILE): offset 0, fd " << dst_fd << "\n";
    std::cout << "  Size: " << xfer_size << " bytes\n\n";
    
    ret = agent.createXferReq(NIXL_READ, read_local_descs, read_remote_descs,
                              agent_name,  // Same agent for local operations
                              &service_chain,
                              read_processed_descs_ptr,  // Pass processed buffer for out-of-place
                              read_req_handle,
                              &extra_params);
    nixl_exit_on_failure(ret, "Failed to create READ transfer request", agent_name);
    
    // Post READ transfer request
    std::cout << "Posting READ transfer request...\n";
    status = agent.postXferReq(read_req_handle);
    nixl_exit_on_failure((status >= NIXL_SUCCESS), "Failed to post READ transfer request", agent_name);
    
    std::cout << "READ transfer posted, waiting for completion...\n";
    
    // Wait for READ transfer completion
    while (status != NIXL_SUCCESS) {
        status = agent.getXferStatus(read_req_handle);
        nixl_exit_on_failure((status >= NIXL_SUCCESS), "READ transfer failed", agent_name);
    }
    
    std::cout << "READ transfer completed successfully!\n\n";
    
    // Verify READ transfer
    std::cout << "Verifying READ transfer (FILE -> DRAM)...\n";
    std::cout << "  Checking first " << xfer_size << " bytes...\n";
    
    bool read_correct = (memcmp(src_buffer, read_dst_buffer, xfer_size) == 0);
    
    if (read_correct) {
        std::cout << "  ✓ READ verification PASSED\n";
        std::cout << "  First 16 bytes of original source:  ";
        for (size_t i = 0; i < 16; i++)
            printf("%02X ", ((uint8_t*)src_buffer)[i]);
        std::cout << "\n  First 16 bytes after READ from file: ";
        for (size_t i = 0; i < 16; i++)
            printf("%02X ", ((uint8_t*)read_dst_buffer)[i]);
        std::cout << "\n\n";
    } else {
        std::cout << "  ✗ READ verification FAILED\n";
    }
    
    nixl_exit_on_failure(read_correct, "Data mismatch after READ transfer", agent_name);
    
    // Release READ transfer request
    ret = agent.releaseXferReq(read_req_handle);
    nixl_exit_on_failure(ret, "Failed to release READ transfer request", agent_name);
    
    // Deregister read buffer memory
    ret = agent.deregisterMem(reg_list_read_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister read buffer memory", agent_name);
    
    // Free read buffer
    free(read_dst_buffer);

    // Cleanup
    std::cout << "\n========================================\n";
    std::cout << "Cleanup\n";
    std::cout << "========================================\n\n";
    
    ret = agent.releaseXferReq(req_handle);
    nixl_exit_on_failure(ret, "Failed to release WRITE transfer request", agent_name);

    // Deregister memory based on mode
    if (use_out_of_place) {
        // Out-of-place: Deregister processed buffer
        nixl_reg_dlist_t dereg_processed(DRAM_SEG);
        nixlBlobDesc dereg_proc_desc;
        dereg_proc_desc.addr = (uintptr_t)processed_buffer;
        dereg_proc_desc.len = buffer_size;
        dereg_proc_desc.devId = 0;
        dereg_processed.addDesc(dereg_proc_desc);
        
        ret = agent.deregisterMem(dereg_processed, &extra_params);
        nixl_exit_on_failure(ret, "Failed to deregister processed buffer", agent_name);
    } else {
        // In-place: Deregister source buffer
        nixl_reg_dlist_t dereg_src(DRAM_SEG);
        nixlBlobDesc dereg_src_desc;
        dereg_src_desc.addr = (uintptr_t)src_buffer;
        dereg_src_desc.len = buffer_size;
        dereg_src_desc.devId = 0;
        dereg_src.addDesc(dereg_src_desc);
        
        ret = agent.deregisterMem(dereg_src, &extra_params);
        nixl_exit_on_failure(ret, "Failed to deregister source memory", agent_name);
    }
    
    // Deregister destination file (always registered)
    nixl_reg_dlist_t dereg_dst(FILE_SEG);
    nixlBlobDesc dereg_dst_desc;
    dereg_dst_desc.addr = 0;
    dereg_dst_desc.len = buffer_size;
    dereg_dst_desc.devId = dst_fd;
    dereg_dst_desc.metaInfo = dst_file_path;
    dereg_dst.addDesc(dereg_dst_desc);
    
    ret = agent.deregisterMem(dereg_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister destination file", agent_name);

    // Cleanup resources
    close(dst_fd);  // Close file descriptor
    free(src_buffer);
    if (use_out_of_place && processed_buffer) {
        free(processed_buffer);
    }
    std::remove(dst_file_path.c_str());
    std::cout << "Cleaned up resources (closed file, freed buffers, removed file)\n";

    std::cout << "\n========================================\n";
    std::cout << "All tests completed successfully!\n";
    std::cout << "  ✓ WRITE transfer (DRAM -> FILE)\n";
    std::cout << "  ✓ READ transfer (FILE -> DRAM)\n";
    std::cout << "  ✓ Service chain applied\n";
    std::cout << "========================================\n";
    
    return 0;
}
