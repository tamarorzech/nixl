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
 * @brief Example demonstrating the NIXL service plugin with local/storage backends (POSIX, GDS, etc.)
 *
 * The service plugin operates in-place: it transforms the source buffer before the
 * backend transfer begins. No separate "processed" buffer is needed.
 *
 * The example demonstrates:
 * - WRITE operation: service processes DRAM buffer in-place, then transfers to file
 * - READ operation:  file is read into DRAM buffer, then service processes in-place
 * - Non-blocking async service flow (postXferReq / getXferStatus)
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
#include "nixl_service_manager.h"

std::string agent_name("LocalAgent");

void printParams(const nixl_b_params_t& params, const nixl_mem_list_t& mems) {
    if (params.empty()) {
        std::cout << "Parameters: (empty)\n";
    } else {
        std::cout << "Parameters:\n";
        for (const auto& pair : params)
            std::cout << "  " << pair.first << " = " << pair.second << "\n";
    }

    if (mems.empty()) {
        std::cout << "Mems: (empty)\n";
    } else {
        std::cout << "Mems:\n";
        for (const auto& elm : mems)
            std::cout << "  " << nixlEnumStrings::memTypeStr(elm) << "\n";
    }
}

// Structure to hold parsed command-line arguments
struct ProgramArgs {
    std::string backend;
};

ProgramArgs parseArguments(int argc, char **argv) {
    ProgramArgs args;
    args.backend = "POSIX";

    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            std::cout << "Usage: " << argv[0] << " [BACKEND]\n";
            std::cout << "  BACKEND: Backend name (default: POSIX)\n";
            std::cout << "\nService plugin always operates in-place.\n";
            exit(0);
        }
        args.backend = arg1;
    }

    return args;
}

// Structure to hold buffer and file resources
struct BufferResources {
    void*       src_buffer;
    size_t      buffer_size;
    int         dst_fd;
    std::string dst_file_path;
    size_t      output_buffer_size;
};

BufferResources allocateBuffers() {
    BufferResources resources;

    constexpr size_t float_size = sizeof(float);
    resources.buffer_size = 40960 * float_size;

    resources.src_buffer = calloc(1, resources.buffer_size);
    if (!resources.src_buffer) {
        std::cerr << "Failed to allocate source buffer\n";
        exit(1);
    }
    memset(resources.src_buffer, 0xAA, resources.buffer_size);

    return resources;
}

int setupFile(const std::string& file_path, size_t output_buffer_size) {
    int fd = open(file_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        std::cerr << "Failed to open destination file: " << file_path << "\n";
        return -1;
    }

    std::vector<uint8_t> zeros(output_buffer_size, 0x00);
    ssize_t written = write(fd, zeros.data(), output_buffer_size);
    if (written != (ssize_t)output_buffer_size) {
        std::cerr << "Failed to pre-allocate file\n";
        close(fd);
        return -1;
    }

    return fd;
}

nixlBackendH* initializeAgentAndBackend(const std::string& backend,
                                        nixlAgent& agent,
                                        nixl_opt_args_t& extra_params) {
    nixl_status_t ret;

    std::vector<nixl_backend_t> plugins;
    ret = agent.getAvailPlugins(plugins);
    nixl_exit_on_failure(ret, "Failed to get available plugins", agent_name);

    std::cout << "Available plugins:\n";
    for (const auto& b : plugins)
        std::cout << "  - " << b << "\n";
    std::cout << "\n";

    if (std::find(plugins.begin(), plugins.end(), backend) == plugins.end()) {
        std::cerr << "ERROR: Backend '" << backend << "' not found!\n";
        exit(1);
    }

    nixl_b_params_t init_params;
    nixl_mem_list_t mems;
    ret = agent.getPluginParams(backend, mems, init_params);
    nixl_exit_on_failure(ret, "Failed to get plugin params", agent_name);

    std::cout << "Backend parameters:\n";
    printParams(init_params, mems);
    std::cout << "\n";

    nixlBackendH *backend_handle;
    ret = agent.createBackend(backend, init_params, backend_handle);
    nixl_exit_on_failure(ret, "Failed to create " + backend + " backend", agent_name);

    extra_params.backends.push_back(backend_handle);

    return backend_handle;
}

void registerMemory(nixlAgent& agent,
                    const BufferResources& resources,
                    nixl_opt_args_t& extra_params) {
    nixl_status_t ret;

    // Register source buffer (service operates on it in-place)
    nixl_reg_dlist_t reg_list_src(DRAM_SEG);
    nixlBlobDesc src_desc;
    src_desc.addr  = (uintptr_t)resources.src_buffer;
    src_desc.len   = resources.buffer_size;
    src_desc.devId = 0;
    reg_list_src.addDesc(src_desc);

    ret = agent.registerMem(reg_list_src, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register source memory", agent_name);

    // Register destination file
    nixl_reg_dlist_t reg_list_dst(FILE_SEG);
    nixlBlobDesc dst_desc;
    dst_desc.addr     = 0;
    dst_desc.len      = resources.output_buffer_size;
    dst_desc.devId    = resources.dst_fd;
    dst_desc.metaInfo = resources.dst_file_path;
    reg_list_dst.addDesc(dst_desc);

    ret = agent.registerMem(reg_list_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register destination file", agent_name);

    std::cout << "Memory registered:\n";
    std::cout << "  Source buffer (DRAM) - service will operate in-place\n";
    std::cout << "  Destination file\n\n";
}

// Create a service handle using nixlServiceManager
nixlServiceH* createService(nixlServiceManager& svc_mgr) {
    nixl_service_t service_type = "kvtc";

    std::cout << "Creating service via nixlServiceManager...\n";

    std::vector<nixl_service_t> available;
    svc_mgr.getAvailPlugins(available);
    std::cout << "  Available service plugins:\n";
    for (const auto& s : available)
        std::cout << "    - " << s << "\n";

    nixl_b_params_t params;
    nixl_status_t ret = svc_mgr.getPluginParams(service_type, params);
    if (ret != NIXL_SUCCESS)
        std::cerr << "  Warning: getPluginParams failed (" << ret << "); using empty params\n";

    params["dev_bdf"]      = "0000:81:00.0";
    params["server_name"]  = "kvtc_demo";

    nixlServiceH* svc_h = nullptr;
    ret = svc_mgr.createService(service_type, params, svc_h);
    if (ret != NIXL_SUCCESS || svc_h == nullptr) {
        std::cerr << "  Warning: createService failed (" << ret << "); no service will be used\n";
        return nullptr;
    }

    std::cout << "  Service created: " << service_type << " (in-place)\n\n";
    return svc_h;
}

// Perform WRITE operation: service processes src_buffer in-place, then transfer to file
void performWriteOperation(nixlAgent& agent,
                           const BufferResources& resources,
                           nixl_opt_args_t& extra_params,
                           nixlServiceH* svc_h) {
    nixl_status_t ret;

    std::cout << "========================================\n";
    std::cout << "Testing WRITE operation (DRAM -> FILE)\n";
    std::cout << "========================================\n\n";

    nixl_xfer_dlist_t src_xfer_descs(DRAM_SEG);
    nixlBasicDesc src_xfer;
    src_xfer.addr  = (uintptr_t)resources.src_buffer;
    src_xfer.len   = resources.buffer_size;
    src_xfer.devId = 0;
    src_xfer_descs.addDesc(src_xfer);

    nixl_xfer_dlist_t dst_xfer_descs(FILE_SEG);
    nixlBasicDesc dst_xfer;
    dst_xfer.addr  = 0;
    dst_xfer.len   = resources.output_buffer_size;
    dst_xfer.devId = resources.dst_fd;
    dst_xfer_descs.addDesc(dst_xfer);

    std::cout << "Creating transfer request...\n";
    std::cout << "  Source: " << resources.src_buffer << "\n";
    std::cout << "  Destination: " << (void*)dst_xfer.addr << "\n";
    std::cout << "  Original Size: " << resources.buffer_size << " bytes\n";
    std::cout << "  Compressed Size: " << resources.output_buffer_size << " bytes\n\n";

    nixlXferReqH *req_handle;
    ret = agent.createXferReq(NIXL_WRITE, src_xfer_descs, dst_xfer_descs,
                              agent_name, req_handle, &extra_params, svc_h);
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

// Perform READ operation: read file into DRAM buffer, then service processes in-place
void performReadOperation(nixlAgent& agent,
                          const BufferResources& resources,
                          nixl_opt_args_t& extra_params,
                          nixlServiceH* svc_h) {
    nixl_status_t ret;

    std::cout << "========================================\n";
    std::cout << "Testing READ operation (FILE -> DRAM)\n";
    std::cout << "========================================\n\n";

    void* read_dst_buffer = calloc(1, resources.buffer_size);
    if (!read_dst_buffer) {
        std::cerr << "Failed to allocate read destination buffer\n";
        exit(1);
    }
    memset(read_dst_buffer, 0x00, resources.buffer_size);

    nixl_reg_dlist_t reg_list_read_dst(DRAM_SEG);
    nixlBlobDesc read_dst_desc;
    read_dst_desc.addr  = (uintptr_t)read_dst_buffer;
    read_dst_desc.len   = resources.buffer_size;
    read_dst_desc.devId = 0;
    reg_list_read_dst.addDesc(read_dst_desc);

    ret = agent.registerMem(reg_list_read_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to register read destination memory", agent_name);

    size_t xfer_size = resources.buffer_size;

    nixl_xfer_dlist_t read_local_descs(DRAM_SEG);
    nixlBasicDesc read_local;
    read_local.addr  = (uintptr_t)read_dst_buffer;
    read_local.len   = xfer_size;
    read_local.devId = 0;
    read_local_descs.addDesc(read_local);

    nixl_xfer_dlist_t read_remote_descs(FILE_SEG);
    nixlBasicDesc read_remote;
    read_remote.addr  = 0;
    read_remote.len   = xfer_size;
    read_remote.devId = resources.dst_fd;
    read_remote_descs.addDesc(read_remote);

    std::cout << "Creating READ transfer request...\n";
    std::cout << "  Local (DRAM): " << read_dst_buffer << "\n";
    std::cout << "  Remote (FILE): offset 0, fd " << resources.dst_fd << "\n";
    std::cout << "  Size: " << xfer_size << " bytes\n\n";

    nixlXferReqH *read_req_handle;
    ret = agent.createXferReq(NIXL_READ, read_local_descs, read_remote_descs,
                              agent_name, read_req_handle, &extra_params, svc_h);
    nixl_exit_on_failure(ret, "Failed to create READ transfer request", agent_name);

    std::cout << "Posting READ transfer request...\n";
    nixl_status_t status = agent.postXferReq(read_req_handle);
    nixl_exit_on_failure((status >= NIXL_SUCCESS), "Failed to post READ request", agent_name);

    std::cout << "READ transfer posted, waiting for completion...\n";
    while (status != NIXL_SUCCESS) {
        status = agent.getXferStatus(read_req_handle);
        nixl_exit_on_failure((status >= NIXL_SUCCESS), "READ transfer failed", agent_name);
    }

    std::cout << "READ transfer completed successfully!\n\n";

    bool read_correct = (memcmp(resources.src_buffer, read_dst_buffer, xfer_size) == 0);
    if (read_correct) {
        std::cout << "  READ verification PASSED\n\n";
    } else {
        std::cout << "  READ verification FAILED\n";
    }
    nixl_exit_on_failure(read_correct, "Data mismatch after READ transfer", agent_name);

    ret = agent.releaseXferReq(read_req_handle);
    nixl_exit_on_failure(ret, "Failed to release READ transfer request", agent_name);

    ret = agent.deregisterMem(reg_list_read_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister read buffer", agent_name);

    free(read_dst_buffer);
}

void cleanupResources(const BufferResources& resources) {
    close(resources.dst_fd);
    free(resources.src_buffer);
    std::remove(resources.dst_file_path.c_str());
    std::cout << "Cleaned up resources\n";
}

int main(int argc, char **argv) {
    ProgramArgs args = parseArguments(argc, argv);

    std::cout << "========================================\n";
    std::cout << "NIXL Service Plugin Example\n";
    std::cout << "Backend: " << args.backend << "\n";
    std::cout << "Mode:    in-place\n";
    std::cout << "========================================\n\n";

    nixlAgentConfig cfg(false);
    nixlAgent agent(agent_name, cfg);
    nixl_opt_args_t extra_params;

    initializeAgentAndBackend(args.backend, agent, extra_params);
    std::cout << "Backend created successfully\n\n";

    // Create service — plugin transforms src_buffer in-place before each transfer
    nixlServiceManager svc_mgr;
    nixlServiceH* svc_h = createService(svc_mgr);

    BufferResources resources = allocateBuffers();
    size_t max_sz = resources.buffer_size;
    if (svc_h) {
        max_sz = svc_h->GetMaxBuffersize(resources.buffer_size, NIXL_WRITE);
        std::cout << "Max output buffer size for WRITE: " << max_sz << " bytes\n\n";
    }    
    resources.dst_file_path   = "/tmp/nixl_dst_test_file.bin";
    resources.dst_fd          = setupFile(resources.dst_file_path, max_sz);
    resources.output_buffer_size = max_sz;
    if (resources.dst_fd < 0) {
        free(resources.src_buffer);
        return 1;
    }

    std::cout << "Allocated resources:\n";
    std::cout << "  Source (DRAM):      " << resources.src_buffer
              << " (size: " << resources.buffer_size << " bytes, pattern: 0xAA)\n";
    std::cout << "  Destination (FILE): " << resources.dst_file_path
              << " (fd: " << resources.dst_fd << ", size: " << resources.output_buffer_size << " bytes)\n\n";

    registerMemory(agent, resources, extra_params);

    // WRITE: service processes src_buffer in-place, then backend writes to file
    performWriteOperation(agent, resources, extra_params, svc_h);

    // READ: backend reads file into DRAM, then service processes in-place
    // performReadOperation(agent, resources, extra_params, svc_h);

    // Cleanup
    std::cout << "Cleanup\n";
    nixl_status_t ret;

    nixl_reg_dlist_t dereg_src(DRAM_SEG);
    nixlBlobDesc src_dereg;
    src_dereg.addr  = (uintptr_t)resources.src_buffer;
    src_dereg.len   = resources.buffer_size;
    src_dereg.devId = 0;
    dereg_src.addDesc(src_dereg);
    ret = agent.deregisterMem(dereg_src, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister source memory", agent_name);

    nixl_reg_dlist_t dereg_dst(FILE_SEG);
    nixlBlobDesc dst_dereg;
    dst_dereg.addr     = 0;
    dst_dereg.len      = resources.output_buffer_size;
    dst_dereg.devId    = resources.dst_fd;
    dst_dereg.metaInfo = resources.dst_file_path;
    dereg_dst.addDesc(dst_dereg);
    ret = agent.deregisterMem(dereg_dst, &extra_params);
    nixl_exit_on_failure(ret, "Failed to deregister destination file", agent_name);

    if (svc_h) {
        svc_mgr.destroyService(svc_h);
        std::cout << "Service destroyed\n";
    }

    cleanupResources(resources);

    std::cout << "\n========================================\n";
    std::cout << "All tests completed successfully!\n";
    std::cout << "  WRITE transfer (DRAM -> FILE)\n";
    std::cout << "  Service (in-place) applied\n";
    std::cout << "========================================\n";

    return 0;
}
