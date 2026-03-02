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

# KVTC Service Plugin
kvtc_sources = [
    'kvtc_service.cpp',
    'kvtc_plugin.cpp',
    'host/host_client.cpp',
    'common/doca_dev_utils.cpp',
]

kvtc_deps = [
    nixl_infra,
    nixl_common_dep,
    dependency('doca-common'),
    dependency('doca-comch'),
]

compile_flags = []

# Build as shared library for dynamic loading
if 'KVTC' in static_plugins
    kvtc_service_lib = static_library('service_kvtc',
        kvtc_sources,
        dependencies: kvtc_deps,
        cpp_args: compile_flags,
        include_directories: [nixl_inc_dirs, utils_inc_dirs, include_directories('common', 'host')],
        install: false,
        name_prefix: 'libnixl_')
else
    kvtc_service_lib = shared_library('service_kvtc',
        kvtc_sources,
        dependencies: kvtc_deps,
        cpp_args: ['-fPIC'],
        include_directories: [nixl_inc_dirs, utils_inc_dirs, include_directories('common', 'host')],
        install: true,
        name_prefix: 'libnixl_',
        install_dir: service_plugin_install_dir,
        install_rpath: '$ORIGIN/..')
    if get_option('buildtype') == 'debug'
        run_command('sh', '-c',
            'echo "kvtc=' + kvtc_service_lib.full_path() + '" >> ' + plugin_build_dir + '/service_pluginlist',
            check: true
        )
    endif
endif

kvtc_service_interface = declare_dependency(link_with: kvtc_service_lib)
