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

#include "nixl_service_chain.h"
#include "nixl_types.h"
#include "nixl_descriptors.h"
#include "backend/backend_aux.h"
#include "common/nixl_log.h"
#include "service/service_engine.h"
#include "plugin_manager.h"
#include <algorithm>

nixlServiceChainStatus nixlServiceChain::addService(nixl_service_t service) {
    // Validate input parameter
    if (service.empty()) {
        NIXL_ERROR << "Cannot add service with empty name";
        return nixlServiceChainStatus::INVALID_PARAM;
    }

    auto& plugin_manager = nixlPluginManager::getInstance();
    
    // Load the service plugin
    auto plugin_handle = plugin_manager.loadServicePlugin(service);
    if (!plugin_handle) {
        NIXL_ERROR << "Failed to load service plugin: " << service;
        return nixlServiceChainStatus::OPERATION_FAILED;
    }
    
    // Create init params for the service engine
    nixlServiceInitParams init_params;
    init_params.type = service;
    init_params.customParams = nullptr;  // No custom params for now
    init_params.flags = 0;
    
    // Create the service engine
    nixlServiceEngine* engine = plugin_handle->createEngine(&init_params);
    if (!engine) {
        NIXL_ERROR << "Failed to create service engine for: " << service;
        return nixlServiceChainStatus::OPERATION_FAILED;
    }
    
    // Create the service handle
    nixlServiceH* service_h = new nixlServiceH(engine);

    // Validate compatibility before adding
    nixlServiceChainStatus status = validateServiceCompatibility(service_h);
    if (status != nixlServiceChainStatus::SUCCESS) {
        delete service_h;
        NIXL_ERROR << "Service '" << service << "' is incompatible with the current chain";
        return status;
    }
    
    // Add the service to the chain
    services_.push_back(service_h);
    
    // Mark chain as valid after successful addition
    chain_valid_ = true;

    NIXL_DEBUG << "Successfully added service '" << service << "' to chain, total services: " << services_.size();
    return nixlServiceChainStatus::SUCCESS;
}

nixlServiceChainStatus nixlServiceChain::removeService(nixl_service_t service) {
    // Validate input
    if (service.empty()) {
        NIXL_ERROR << "Cannot remove service with empty name";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Find the service by name (compare type strings)
    auto it = std::find_if(services_.begin(), services_.end(),
                          [&service](const nixlServiceH* s) {
                              return s != nullptr && s->getType() == service;
                          });
    
    if (it == services_.end()) {
        NIXL_ERROR << "Service '" << service << "' not found in chain";
        return nixlServiceChainStatus::NOT_FOUND;
    }
    
    // Validate that removal won't break the chain
    nixlServiceChainStatus status = validateRemoval(it);
    if (status != nixlServiceChainStatus::SUCCESS) {
        return status;
    }

    // Remove the service from the chain (but don't delete it - managed externally)
    services_.erase(it);
    
    // Mark chain as valid after successful removal (removal validation already passed)
    // If chain is now empty, it's valid; otherwise validation was done by validateRemoval()
    chain_valid_ = true;
    
    NIXL_DEBUG << "Successfully removed service '" << service << "' from chain, remaining services: " << services_.size();
    return nixlServiceChainStatus::SUCCESS;
}

nixlServiceChainStatus nixlServiceChain::validateServiceCompatibility(nixlServiceH* new_service) const {
    // Validate service pointer
    if (new_service == nullptr) {
        NIXL_ERROR << "Cannot validate null service";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Empty chain - any service is compatible
    if (services_.empty()) {
        return nixlServiceChainStatus::SUCCESS;
    }
    
    // Get the last service in the chain
    nixlServiceH* last_service = services_.back();
    if (last_service == nullptr) {
        NIXL_ERROR << "Null service found at end of chain";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Get supported memory types for both services
    nixl_mem_list_t last_mems = last_service->engine->getSupportedMems();
    nixl_mem_list_t new_mems = new_service->engine->getSupportedMems();
    
    // Check if there's any overlap in supported memory types
    for (const auto& last_mem : last_mems) {
        for (const auto& new_mem : new_mems) {
            if (last_mem == new_mem) {
                NIXL_DEBUG << "Services compatible via memory type: " << last_mem;
                return nixlServiceChainStatus::SUCCESS;
            }
        }
    }
    
    NIXL_ERROR << "No compatible memory types between service '" << last_service->getType() 
               << "' and new service '" << new_service->getType() << "'";
    return nixlServiceChainStatus::NOT_ALLOWED;
}

nixlServiceChainStatus nixlServiceChain::validateRemoval(std::vector<nixlServiceH*>::const_iterator it) const {
    // Validate iterator
    if (it == services_.end()) {
        NIXL_ERROR << "Invalid iterator for removal validation";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    size_t index = std::distance(services_.cbegin(), it);
    
    // If it's the first or last service, removal is always safe
    if (index == 0 || index == services_.size() - 1) {
        return nixlServiceChainStatus::SUCCESS;
    }
    
    // Check if services before and after have compatible memory types
    nixlServiceH* prev_service = services_[index - 1];
    nixlServiceH* next_service = services_[index + 1];
    
    if (prev_service == nullptr || next_service == nullptr) {
        NIXL_ERROR << "Null service found in chain";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Get supported memory types
    nixl_mem_list_t prev_mems = prev_service->engine->getSupportedMems();
    nixl_mem_list_t next_mems = next_service->engine->getSupportedMems();
    
    // Check for overlap
    for (const auto& prev_mem : prev_mems) {
        for (const auto& next_mem : next_mems) {
            if (prev_mem == next_mem) {
                NIXL_DEBUG << "Removal safe: services remain compatible via memory type: " << prev_mem;
                return nixlServiceChainStatus::SUCCESS;
            }
        }
    }
    
    NIXL_ERROR << "Removing service '" << (*it)->getType() << "' would break memory type compatibility "
               << "between '" << prev_service->getType() << "' and '" << next_service->getType() << "'";
    return nixlServiceChainStatus::NOT_ALLOWED;
}

// validateChain() is now replaced by isValid() inline method in the header

nixlServiceChainStatus nixlServiceChain::operateServices(const nixl_xfer_op_t operation, const nixl_xfer_dlist_t& input_buffers,
                                                         nixl_xfer_dlist_t* output_buffers) {
    // Validate chain before operating
    if (services_.empty()) {
        NIXL_ERROR << "Cannot operate on empty service chain";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Validate input buffers
    if (input_buffers.isEmpty()) {
        NIXL_ERROR << "Cannot operate with empty input buffer descriptors";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Check if the chain is valid
    if (!isValid()) {
        NIXL_ERROR << "Service chain is not valid";
        return nixlServiceChainStatus::INVALID_PARAM;
    }

    NIXL_DEBUG << "Executing " << services_.size() << " services in chain with " 
               << input_buffers.descCount() << " input buffer(s)";

    // Convert nixl_xfer_dlist_t (BasicDesc) to vector<nixlBlobDesc> for service processing
    std::vector<nixlBlobDesc> blob_descs;
    blob_descs.reserve(input_buffers.descCount());
    
    for (int i = 0; i < input_buffers.descCount(); i++) {
        const auto& desc = input_buffers[i];
        // Create BlobDesc from BasicDesc (no metadata for basic descriptors)
        blob_descs.emplace_back(desc.addr, desc.len, desc.devId, nixl_blob_t{});
    }
    
    // Execute each service in the chain
    for (auto service : services_) {
        nixl_status_t status = service->engine->processData(operation, blob_descs);
        if (status != NIXL_SUCCESS) {
            NIXL_ERROR << "Service '" << service->getType() << "' processing failed with status " << status;
            return nixlServiceChainStatus::OPERATION_FAILED;
        }
    }
               
    return nixlServiceChainStatus::SUCCESS;
}

nixlServiceChainStatus nixlServiceChain::operateServices(const nixl_xfer_op_t operation,const nixl_meta_dlist_t& input_buffers,
                                                         nixl_meta_dlist_t* output_buffers) {
    // Validate chain before operating
    if (services_.empty()) {
        NIXL_ERROR << "Cannot operate on empty service chain";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Validate input buffers
    if (input_buffers.isEmpty()) {
        NIXL_ERROR << "Cannot operate with empty input buffer descriptors";
        return nixlServiceChainStatus::INVALID_PARAM;
    }
    
    // Check if the chain is valid
    if (!isValid()) {
        NIXL_ERROR << "Service chain is not valid";
        return nixlServiceChainStatus::INVALID_PARAM;
    }

    NIXL_INFO << "Executing " << services_.size() << " services in chain with " 
               << input_buffers.descCount() << " input buffer(s)";

    // Convert nixl_meta_dlist_t (MetaDesc) to vector<nixlBlobDesc> for service processing
    // MetaDesc extends BasicDesc, so we can access addr, len, devId fields
    std::vector<nixlBlobDesc> blob_descs;
    blob_descs.reserve(input_buffers.descCount());
    
    for (int i = 0; i < input_buffers.descCount(); i++) {
        const auto& desc = input_buffers[i];
        // Create BlobDesc from MetaDesc (no metadata extraction for now)
        blob_descs.emplace_back(desc.addr, desc.len, desc.devId, nixl_blob_t{});
    }
    
    // Execute each service in the chain
    for (auto service : services_) {
        nixl_status_t status = service->engine->processData(operation, blob_descs);
        if (status != NIXL_SUCCESS) {
            NIXL_ERROR << "Service '" << service->getType() << "' processing failed with status " << status;
            return nixlServiceChainStatus::OPERATION_FAILED;
        }
    }
               
    return nixlServiceChainStatus::SUCCESS;
}
