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
#ifndef _NIXL_SERVICE_CHAIN_H
#define _NIXL_SERVICE_CHAIN_H

#include <vector>
#include <cstddef>

#include "service/service_engine.h"

// Forward declarations - we don't need to include full headers
template<class T> class nixlDescList;
class nixlBasicDesc;
class nixlMetaDesc;
using nixl_xfer_dlist_t = nixlDescList<nixlBasicDesc>;
using nixl_meta_dlist_t = nixlDescList<nixlMetaDesc>;

// This class hides away the nixlServiceEngine from user of the Agent API
class nixlServiceH {
    private:
        nixlServiceEngine* engine;
        nixlServiceH(nixlServiceEngine* engine) : engine(engine) {}
        ~nixlServiceH () {}

    public:
        
        nixl_service_t getType () const { return engine->getType(); }
        
    friend class nixlServiceChain;
};

/**
 * @enum nixlServiceChainStatus
 * @brief Status codes for service chain operations
 */
enum class nixlServiceChainStatus {
    SUCCESS = 0,                  // Operation completed successfully
    INVALID_PARAM = -2,           // Invalid parameter provided (e.g., nullptr)
    NOT_FOUND = -4,               // Service not found in chain
    NOT_ALLOWED = -6,             // Operation violates chain constraints
    OPERATION_FAILED = -8         // Service operation failed during execution
};

/**
 * @class nixlServiceChain
 * @brief A class that manages a chain of service engines to be applied sequentially
 *        to data transfers. The chain allows multiple services to execute in a
 *        specified order determined by the order in which services are appended.
 *
 * The nixlServiceChain class provides:
 * - Sequential execution of multiple services in a specified order
 * - Validation of service order concerning memory type and service conditions
 * - Verification that buffers at the end of the chain are properly registered
 *   when handed over to the transfer backend
 */
class nixlServiceChain {
private:
    /** @var services_ Internal vector holding the service engine pointers */
    std::vector<nixlServiceH*> services_;
    
    /** @var chain_valid_ Validation state of the chain */
    bool chain_valid_;
    
    /**
     * @brief Validate compatibility of a new service with the chain
     *
     * Checks if the new service's supported memory types overlap with
     * the last service in the chain (if any exists).
     *
     * @param new_service Service to validate for addition
     * @return nixlServiceChainStatus 
     *         - SUCCESS: Service is compatible
     *         - INVALID_PARAM: Service is null
     *         - NOT_ALLOWED: Memory type incompatibility detected
     */
    nixlServiceChainStatus validateServiceCompatibility(nixlServiceH* new_service) const;
    
    /**
     * @brief Validate that removing a service maintains chain compatibility
     *
     * Checks if removing the service at the given iterator position would break 
     * memory type compatibility between the services before and after it.
     *
     * @param it Iterator pointing to the service to check for removal
     * @return nixlServiceChainStatus
     *         - SUCCESS: Removal is safe
     *         - INVALID_PARAM: Iterator is invalid
     *         - NOT_ALLOWED: Removal would break compatibility
     */
    nixlServiceChainStatus validateRemoval(std::vector<nixlServiceH*>::const_iterator it) const;
    
    /**
     * @brief Check if the service chain is valid
     *
     * Returns the cached validation state of the chain. The state is updated
     * whenever services are added or removed.
     *
     * @return bool true if chain is valid, false otherwise
     */
    bool isValid() const { return chain_valid_; }

public:
    /**
     * @brief Default constructor for nixlServiceChain
     * 
     * Initializes chain as invalid. Chain becomes valid after first successful 
     * service addition or when validated explicitly.
     */
    nixlServiceChain() : chain_valid_(false) {}

    /**
     * @brief Destructor for nixlServiceChain
     * @note Does not delete the service pointers; they are managed by nixlAgent
     */
    ~nixlServiceChain() = default;

    /**
     * @brief Copy constructor
     */
    nixlServiceChain(const nixlServiceChain&) = default;

    /**
     * @brief Move constructor
     */
    nixlServiceChain(nixlServiceChain&&) noexcept = default;

    /**
     * @brief Copy assignment operator
     */
    nixlServiceChain& operator=(const nixlServiceChain&) = default;

    /**
     * @brief Move assignment operator
     */
    nixlServiceChain& operator=(nixlServiceChain&&) noexcept = default;

    /**
     * @brief Add a service to the end of the chain
     *
     * Services are executed in the order they are added. Upon adding a service,
     * the chain validates that the service can be legally placed at this position
     * based on memory type compatibility and service conditions.
     *
     * @param service Pointer to the service engine to add
     * @return nixlServiceChainStatus Status code
     *         - SUCCESS: Service added successfully
     *         - INVALID_PARAM: Service is nullptr
     *         - NOT_ALLOWED: Service violates chain constraints
     */
    nixlServiceChainStatus addService(nixl_service_t service);

    /**
     * @brief Remove a service from the chain
     *
     * Removes the specified service from the chain. If the service appears
     * multiple times, only the first occurrence is removed.
     *
     * @param service Pointer to the service engine to remove
     * @return nixlServiceChainStatus Status code
     *         - SUCCESS: Service removed successfully and chain is still valid
     *         - NOT_FOUND: Service not in chain
     *         - NOT_ALLOWED: Service removal violates chain constraints
     *         - OPERATION_FAILED: Service removal failed during execution
     *         - INVALID_PARAM: Service is nullptr
     */
    nixlServiceChainStatus removeService(nixl_service_t service);

    /**
     * @brief Execute all services in the chain sequentially
     *
     * Applies each service in the chain to the data in the order they were added.
     * If any service operation fails, the chain execution stops and an error is returned.
     * Each service transforms the input buffers and produces output buffers that become
     * the input for the next service in the chain.
     *
     * @param input_buffers Input buffer descriptors for the first service in the chain
     * @param output_buffers Optional pointer to output buffer descriptors from the last 
     *                       service in the chain. If nullptr, output buffers are not returned.
     * @return nixlServiceChainStatus Status code
     *         - SUCCESS: All services executed successfully
     *         - INVALID_PARAM: Chain is empty, invalid, or buffer descriptors are invalid
     *         - OPERATION_FAILED: A service operation failed during execution
     */
    nixlServiceChainStatus operateServices(const nixl_xfer_op_t operation, const nixl_xfer_dlist_t& input_buffers,
                                          nixl_xfer_dlist_t* output_buffers = nullptr);

    /**
     * @brief Execute all services in the chain on the given data buffers (overload for meta descriptors)
     *
     * This overload accepts nixl_meta_dlist_t which is used in transfer requests.
     * It extracts the basic descriptor information for service processing.
     *
     * @param input_buffers Input descriptor list with metadata to process
     * @param output_buffers Optional output descriptor list (can be nullptr for in-place)
     * @return nixlServiceChainStatus Status of the operation
     */
    nixlServiceChainStatus operateServices(const nixl_xfer_op_t operation, const nixl_meta_dlist_t& input_buffers,
                                          nixl_meta_dlist_t* output_buffers = nullptr);

    /**
     * @brief Get the vector of service pointers
     * @return const reference to the internal service vector
     */
    const std::vector<nixlServiceH*>& getServices() const {
        return services_;
    }

    /**
     * @brief Get the number of services in the chain
     * @return Number of services
     */
    size_t size() const {
        return services_.size();
    }

    /**
     * @brief Check if the chain is empty
     * @return true if chain contains no services, false otherwise
     */
    bool empty() const {
        return services_.empty();
    }
};

#endif // _NIXL_SERVICE_CHAIN_H
