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

if enabled_plugins.get('KVTC')
    subdir('kvtc')
endif

if not is_disabler(dpu_manager_dep)
    subdir('generic_dpu')
endif

# Create a unified service_plugins directory for easier discovery during development
unified_service_plugins_dir = meson.current_build_dir() / '../core/service_plugins'
service_plugins_src_dir = meson.current_build_dir()

custom_target('unified_service_plugins',
    output: 'unified_service_plugins.stamp',
    build_by_default: true,
    command: ['sh', '-c',
              'mkdir -p ' + unified_service_plugins_dir + ' && ' +
              'for d in ' + service_plugins_src_dir + '/*/; do ' +
              'for f in "$d"*.so; do ' +
              '[ -f "$f" ] && ln -sf "$f" ' + unified_service_plugins_dir + '/ || true; ' +
              'done; done && touch @OUTPUT@']
)
