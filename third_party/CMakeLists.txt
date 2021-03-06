#
# Copyright 2020 WebAssembly Community Group participants
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

## fmt
add_library(libfmt
  fmt/src/format.cc
)

if (MSVC)
  target_compile_options(libfmt
    PRIVATE
    /Zc:preprocessor-
  )
endif ()

target_include_directories(libfmt
  PUBLIC
  ${wasp_SOURCE_DIR}/third_party/fmt/include
)


## gtest
if (BUILD_TESTING)
  set(gtest_force_shared_crt ON CACHE BOOL "Force gtest to use shared library")
  add_subdirectory(gtest)
endif ()

add_subdirectory(gdtoa)
