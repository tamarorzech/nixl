#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
nixl_service_example.py

Python equivalent of examples/cpp/nixl_service_example.cpp.

Demonstrates the NIXL service plugin with a local storage backend (POSIX by default).
The service operates in-place on the DRAM buffer before/after each transfer:

  - WRITE: service processes the DRAM buffer in-place, then backend writes to file.
  - READ:  backend reads the file into the DRAM buffer, then service processes in-place.

Usage:
    python nixl_service_example.py [--backend POSIX] [--service kvtc]
                                   [--dev-bdf 0000:81:00.0] [--server-name kvtc_demo]

Environment variables:
    NIXL_PLUGIN_DIR         Directory for backend plugins (default: auto-detected)
    NIXL_SERVICE_PLUGIN_DIR Directory for service plugins (default: auto-detected)
"""

import argparse
import ctypes
import os
import sys

from nixl._api import nixl_agent, nixl_agent_config, nixl_service_manager
from nixl.logging import get_logger

logger = get_logger(__name__)

# --------------------------------------------------------------------------- #
# Constants
# --------------------------------------------------------------------------- #
DST_FILE    = "/tmp/nixl_py_service_test.bin"
BUFFER_SIZE = 40960 * 4   # 40960 floats x 4 bytes = 160 KB
FILL_BYTE   = 0xAA


# --------------------------------------------------------------------------- #
# Argument parsing
# --------------------------------------------------------------------------- #
def parse_args():
    parser = argparse.ArgumentParser(
        description="NIXL Python service plugin example (in-place DRAM <-> FILE)"
    )
    parser.add_argument(
        "--backend",
        default="POSIX",
        help="Storage backend to use (default: POSIX)",
    )
    parser.add_argument(
        "--service",
        default="kvtc",
        help="Service plugin name (default: kvtc)",
    )
    parser.add_argument(
        "--dev-bdf",
        default="0000:81:00.0",
        help="Device BDF for the service plugin (default: 0000:81:00.0)",
    )
    parser.add_argument(
        "--server-name",
        default="kvtc_demo",
        help="Server name for the service plugin (default: kvtc_demo)",
    )
    return parser.parse_args()


# --------------------------------------------------------------------------- #
# Helper: post a transfer and poll to completion
# --------------------------------------------------------------------------- #
def run_transfer(agent, handle, label):
    state = agent.transfer(handle)
    assert state != "ERR", f"{label}: posting transfer failed"
    while True:
        state = agent.check_xfer_state(handle)
        assert state != "ERR", f"{label}: transfer reached error state"
        if state == "DONE":
            break
    return state


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def main():
    args = parse_args()

    print("=" * 52)
    print("NIXL Python Service Plugin Example")
    print(f"  Backend : {args.backend}")
    print(f"  Service : {args.service}  (in-place)")
    print("=" * 52)
    print()

    # ----------------------------------------------------------------------- #
    # Agent + backend setup
    # ----------------------------------------------------------------------- #
    config = nixl_agent_config(backends=[])
    agent  = nixl_agent("LocalAgent", config)

    available_backends = agent.get_plugin_list()
    print(f"Available backend plugins : {available_backends}")

    if args.backend not in available_backends:
        logger.error(
            "Backend '%s' not found! Set NIXL_PLUGIN_DIR correctly.", args.backend
        )
        sys.exit(1)

    agent.create_backend(args.backend)
    print(f"Backend '{args.backend}' created\n")

    # ----------------------------------------------------------------------- #
    # Allocate DRAM source buffer (ctypes keeps it alive)
    # ----------------------------------------------------------------------- #
    src_buf  = ctypes.create_string_buffer(bytes([FILL_BYTE] * BUFFER_SIZE), BUFFER_SIZE)
    src_addr = ctypes.addressof(src_buf)
    print(
        f"Source buffer  : addr=0x{src_addr:x}, size={BUFFER_SIZE} bytes, "
        f"fill=0x{FILL_BYTE:02X}"
    )

    # ----------------------------------------------------------------------- #
    # Open / pre-allocate destination file
    # ----------------------------------------------------------------------- #
    dst_fd = os.open(DST_FILE, os.O_CREAT | os.O_RDWR | os.O_TRUNC, 0o644)
    os.write(dst_fd, bytes(BUFFER_SIZE))   # pre-allocate with zeros
    print(f"Destination file: {DST_FILE}  (fd={dst_fd}, size={BUFFER_SIZE} bytes)\n")

    # ----------------------------------------------------------------------- #
    # Register memory with NIXL
    #   DRAM reg tuple : (addr, len, devId, metaInfo_str)
    #   FILE reg tuple : (offset, len, fd, file_path)
    # ----------------------------------------------------------------------- #
    src_reg = agent.register_memory([(src_addr, BUFFER_SIZE, 0, "")], "DRAM")
    assert src_reg is not None, "Failed to register source DRAM buffer"

    dst_reg = agent.register_memory([(0, BUFFER_SIZE, dst_fd, DST_FILE)], "FILE")
    assert dst_reg is not None, "Failed to register destination file"

    # trim() strips metaInfo from a RegDList, returning an XferDList
    dst_xfer = dst_reg.trim()

    print("Memory registered:")
    print("  DRAM source buffer  (service will process in-place)")
    print("  FILE destination\n")

    # ----------------------------------------------------------------------- #
    # Service setup via nixl_service_manager
    # ----------------------------------------------------------------------- #
    svc_mgr = nixl_service_manager()

    available_services = svc_mgr.get_avail_plugins()
    print(f"Available service plugins : {available_services}")

    svc_h = None
    if args.service in available_services:
        params = svc_mgr.get_plugin_params(args.service)
        params["dev_bdf"]     = args.dev_bdf
        params["server_name"] = args.server_name

        svc_h = svc_mgr.create_service(args.service, params)
        if svc_h is None:
            logger.warning("createService returned None; proceeding without service")
        else:
            print(f"Service '{args.service}' created (in-place)")
            print(f"  Supported input  mems : {svc_h.getSupportedInputMems()}")
            print(f"  Supported output mems : {svc_h.getSupportedOutputMems()}")
    else:
        logger.warning(
            "Service '%s' not found (set NIXL_SERVICE_PLUGIN_DIR); "
            "proceeding without service",
            args.service,
        )
    print()

    # ----------------------------------------------------------------------- #
    # WRITE: service transforms src_buf in-place -> backend writes to file
    # ----------------------------------------------------------------------- #
    print("=" * 52)
    print("WRITE  (DRAM -> FILE)")
    print("=" * 52)

    src_xfer = agent.get_xfer_descs([(src_addr, BUFFER_SIZE, 0)], "DRAM")

    write_handle = agent.initialize_xfer(
        "WRITE", src_xfer, dst_xfer, "LocalAgent", service_h=svc_h
    )
    assert write_handle, "Failed to create WRITE transfer request"

    run_transfer(agent, write_handle, "WRITE")
    print("WRITE completed successfully\n")
    agent.release_xfer_handle(write_handle)

    # ----------------------------------------------------------------------- #
    # READ: backend reads file -> fresh DRAM buffer; service processes in-place
    # ----------------------------------------------------------------------- #
    print("=" * 52)
    print("READ   (FILE -> DRAM)")
    print("=" * 52)

    # Fresh buffer for round-trip verification (starts as zeros)
    read_buf  = ctypes.create_string_buffer(BUFFER_SIZE)
    read_addr = ctypes.addressof(read_buf)

    read_reg = agent.register_memory([(read_addr, BUFFER_SIZE, 0, "")], "DRAM")
    assert read_reg is not None, "Failed to register read buffer"

    read_xfer = agent.get_xfer_descs([(read_addr, BUFFER_SIZE, 0)], "DRAM")

    read_handle = agent.initialize_xfer(
        "READ", read_xfer, dst_xfer, "LocalAgent", service_h=svc_h
    )
    assert read_handle, "Failed to create READ transfer request"

    run_transfer(agent, read_handle, "READ")
    print("READ completed successfully")

    # Round-trip verification: read_buf should match src_buf after READ + service
    src_bytes  = bytes(src_buf)
    read_bytes = bytes(read_buf)
    if src_bytes == read_bytes:
        print("  Round-trip verification PASSED\n")
    else:
        mismatches = sum(a != b for a, b in zip(src_bytes, read_bytes))
        print(f"  Round-trip verification FAILED: {mismatches}/{BUFFER_SIZE} bytes differ\n")

    agent.release_xfer_handle(read_handle)

    # ----------------------------------------------------------------------- #
    # Cleanup
    # ----------------------------------------------------------------------- #
    print("Cleanup...")
    agent.deregister_memory(read_reg)
    agent.deregister_memory(src_reg)
    agent.deregister_memory(dst_reg)

    if svc_h is not None:
        svc_mgr.destroy_service(svc_h)
        print("  Service destroyed")

    os.close(dst_fd)
    os.remove(DST_FILE)
    print(f"  Temp file '{DST_FILE}' removed")

    print()
    print("=" * 52)
    print("All operations completed successfully!")
    print("  WRITE (DRAM -> FILE)  service in-place")
    print("  READ  (FILE -> DRAM)  service in-place")
    print("=" * 52)


if __name__ == "__main__":
    main()
