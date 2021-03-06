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

add_executable(wasp_binary_unittests
  constants.cc
  formatters_test.cc
  lazy_expression_test.cc
  lazy_linking_section_test.cc
  lazy_module_test.cc
  lazy_module_utils_test.cc
  lazy_name_section_test.cc
  lazy_relocation_section_test.cc
  lazy_section_test.cc
  lazy_sequence_test.cc
  read_test.cc
  read_linking_test.cc
  visitor_test.cc
  write_test.cc
)

target_compile_options(wasp_binary_unittests
  PRIVATE
  ${warning_flags}
)

target_link_libraries(wasp_binary_unittests
  libwasp_binary
  libwasp_test
  gmock
  gmock_main
)

add_test(
  NAME test_binary_unittests
  COMMAND $<TARGET_FILE:wasp_binary_unittests>)
