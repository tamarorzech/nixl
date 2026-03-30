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

# Generic DPU Service Plugin
generic_dpu_sources = [
    'generic_dpu_engine.cpp',
    'generic_dpu_plugin.cpp',
]

generic_dpu_deps = [
    nixl_infra,
    nixl_common_dep,
    dpu_manager_dep,
]

shared_library('service_generic_dpu',
    generic_dpu_sources,
    dependencies: generic_dpu_deps,
    cpp_args: ['-fPIC'],
    include_directories: [nixl_inc_dirs, utils_inc_dirs],
    install: true,
    name_prefix: 'libnixl_',
    install_dir: service_plugin_install_dir,
    install_rpath: '$ORIGIN/..')
