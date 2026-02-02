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

nixlKvtcServiceEngine::nixlKvtcServiceEngine(const nixlServiceInitParams* init_params)
    : nixlServiceEngine(init_params) {
    NIXL_DEBUG << "KVTC service engine created (in-place dummy service)";
}

nixl_mem_list_t nixlKvtcServiceEngine::getSupportedMems() const {
    // Support all memory types since this is a dummy in-place service
    return {DRAM_SEG, VRAM_SEG, BLK_SEG, OBJ_SEG, FILE_SEG};
}

nixl_status_t nixlKvtcServiceEngine::processData(const nixl_xfer_op_t &operation,
                                                  const std::vector<nixlBlobDesc> &data_descs) {
    // Dummy implementation - does nothing, operates in-place
    (void)operation;  // Suppress unused warning
    (void)data_descs; // Suppress unused warning
        
    NIXL_DEBUG << "KVTC service processData called (no-op) for " 
               << data_descs.size() << " descriptor(s)";
    
    // Since this is an in-place service, we don't modify anything
    return NIXL_SUCCESS;
}
