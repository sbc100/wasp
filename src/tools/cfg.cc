//
// Copyright 2019 WebAssembly Community Group participants
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "wasp/base/features.h"
#include "wasp/base/file.h"
#include "wasp/base/format.h"
#include "wasp/base/formatters.h"
#include "wasp/base/optional.h"
#include "wasp/base/string_view.h"
#include "wasp/binary/errors_nop.h"
#include "wasp/binary/formatters.h"
#include "wasp/binary/lazy_code_section.h"
#include "wasp/binary/lazy_export_section.h"
#include "wasp/binary/lazy_expression.h"
#include "wasp/binary/lazy_function_names_subsection.h"
#include "wasp/binary/lazy_function_section.h"
#include "wasp/binary/lazy_import_section.h"
#include "wasp/binary/lazy_module.h"
#include "wasp/binary/lazy_name_section.h"

namespace wasp {
namespace tools {
namespace cfg {

using namespace ::wasp::binary;

using ErrorsType = ErrorsNop;

struct Options {
  Features features;
  string_view function;
  string_view output_filename;
};

using BBID = u32;
constexpr BBID InvalidBBID = ~0;

struct Successor {
  std::string name;
  BBID bbid;
};

struct BasicBlock {
  bool empty() const { return code.empty(); }

  SpanU8 code;
  std::vector<Successor> successors;
};

struct Label {
  Opcode opcode;
  BBID parent;
  BBID br;
  BBID next;
};

struct Tool {
  explicit Tool(SpanU8 data, Options);

  int Run();
  void DoPrepass();
  optional<Index> GetFunctionIndex();
  optional<Code> GetCode(Index);
  void CalculateCFG(Code);
  void RemoveEmptyBasicBlocks();
  void WriteDotFile();

  void InsertFunctionName(Index, string_view name);

  void PushLabel(Opcode, BBID br, BBID next);
  Label PopLabel();
  BBID NewBasicBlock();
  BasicBlock& GetBasicBlock(BBID);
  void StartBasicBlock(BBID, const u8*);
  void EndBasicBlock(const u8*);
  void MarkUnreachable(const u8*);
  void AddSuccessor(BBID, const std::string& name = std::string{});
  void AddSuccessor(BBID, BBID, const std::string& name = std::string{});
  void Br(Index, const std::string& name = std::string{});

  ErrorsType errors;
  Options options;
  LazyModule<ErrorsType> module;
  std::map<string_view, Index> name_to_function;
  Index imported_function_count = 0;
  std::vector<Label> labels;
  std::vector<BasicBlock> cfg;
  BBID start_bbid = InvalidBBID;
  BBID current_bbid = InvalidBBID;
  BBID next_bbid = 0;
};

int Main(int argc, char** argv) {
  string_view filename;
  Options options;
  options.features.EnableAll();

  for (int i = 0; i < argc; ++i) {
    string_view arg = argv[i];
    if (arg[0] == '-') {
      switch (arg[1]) {
        case 'o': options.output_filename = argv[++i]; break;
        case 'f': options.function = argv[++i]; break;
        case '-':
          if (arg == "--output") {
            options.output_filename = argv[++i];
          } else if (arg == "--function") {
            options.function = argv[++i];
          } else {
            print("Unknown long argument {}\n", arg);
          }
          break;
        default:
          print("Unknown short argument {}\n", arg[0]);
          break;
      }
    } else {
      if (filename.empty()) {
        filename = arg;
      } else {
        print("Filename already given\n");
      }
    }
  }

  if (filename.empty()) {
    print("No filenames given.\n");
    return 1;
  }

  if (options.function.empty()) {
    print("No function given.\n");
    return 1;
  }

  auto optbuf = ReadFile(filename);
  if (!optbuf) {
    print("Error reading file {}.\n", filename);
    return 1;
  }

  SpanU8 data{*optbuf};
  Tool tool{data, options};
  return tool.Run();
}

Tool::Tool(SpanU8 data, Options options)
    : options{options}, module{ReadModule(data, options.features, errors)} {}

int Tool::Run() {
  DoPrepass();
  auto index_opt = GetFunctionIndex();
  if (!index_opt) {
    print("Unknown function {}\n", options.function);
    return 1;
  }
  auto code_opt = GetCode(*index_opt);
  if (!code_opt) {
    print("Invalid function index {}\n", *index_opt);
    return 1;
  }
  CalculateCFG(*code_opt);
  RemoveEmptyBasicBlocks();
  WriteDotFile();
  return 0;
}

void Tool::DoPrepass() {
  const Features& features = options.features;
  for (auto section : module.sections) {
    if (section.is_known()) {
      auto known = section.known();
      switch (known.id) {
        case SectionId::Import:
          for (auto import :
               ReadImportSection(known, features, errors).sequence) {
            if (import.kind() == ExternalKind::Function) {
              InsertFunctionName(imported_function_count++, import.name);
            }
          }
          break;

        case SectionId::Export:
          for (auto export_ :
               ReadExportSection(known, features, errors).sequence) {
            if (export_.kind == ExternalKind::Function) {
              InsertFunctionName(export_.index, export_.name);
            }
          }
          break;

        default:
          break;
      }
    } else if (section.is_custom()) {
      auto custom = section.custom();
      if (custom.name == "name") {
        for (auto subsection : ReadNameSection(custom, features, errors)) {
          if (subsection.id == NameSubsectionId::FunctionNames) {
            for (auto name_assoc :
                 ReadFunctionNamesSubsection(subsection, features, errors)
                     .sequence) {
              InsertFunctionName(name_assoc.index, name_assoc.name);
            }
          }
        }
      }
    }
  }
}

optional<Index> Tool::GetFunctionIndex() {
  // Search by name.
  auto iter = name_to_function.find(options.function);
  if (iter != name_to_function.end()) {
    return iter->second;
  }

  // Try to convert the string to an integer and search by index.
  std::string function_str = options.function.to_string();
  size_t pos = 0;
  auto index = std::stoul(function_str, &pos);
  if (pos == function_str.length()) {
    return static_cast<Index>(index);
  }

  return nullopt;
}

optional<Code> Tool::GetCode(Index find_index) {
  for (auto section : module.sections) {
    if (section.is_known()) {
      auto known = section.known();
      if (known.id == SectionId::Code) {
        auto section = ReadCodeSection(known, options.features, errors);
        Index function_index = imported_function_count;
        for (auto code : section.sequence) {
          if (function_index == find_index) {
            return code;
          }
          ++function_index;
        }
      }
    }
  }
  return nullopt;
}

void Tool::CalculateCFG(Code code) {
  const u8* ptr = code.body.data.data();
  PushLabel(Opcode::Return, InvalidBBID, InvalidBBID);
  start_bbid = NewBasicBlock();
  StartBasicBlock(start_bbid, ptr);

  const u8* prev_ptr = ptr;
  auto instrs = ReadExpression(code.body, options.features, errors);
  for (auto it = instrs.begin(), end = instrs.end(); it != end;
       ++it, prev_ptr = ptr) {
    const auto& instr = *it;
    ptr = it.data().data();
    switch (instr.opcode) {
      case Opcode::Unreachable:
        MarkUnreachable(ptr);
        break;

      case Opcode::Block: {
        auto next = NewBasicBlock();
        PushLabel(instr.opcode, next, next);
        break;
      }

      case Opcode::Loop: {
        auto loop = NewBasicBlock();
        auto next = NewBasicBlock();
        AddSuccessor(loop);
        PushLabel(instr.opcode, loop, next);
        StartBasicBlock(loop, prev_ptr);
        break;
      }

      case Opcode::If: {
        auto true_ = NewBasicBlock();
        auto next = NewBasicBlock();
        AddSuccessor(true_, "T");
        PushLabel(instr.opcode, next, next);
        StartBasicBlock(true_, ptr);
        break;
      }

      case Opcode::Else: {
        auto top = PopLabel();
        AddSuccessor(top.next);
        auto false_ = NewBasicBlock();
        AddSuccessor(top.parent, false_, "F");
        PushLabel(instr.opcode, top.next, top.next);
        StartBasicBlock(false_, ptr);
        break;
      }

      case Opcode::End: {
        auto top = PopLabel();
        AddSuccessor(top.next);
        if (top.opcode == Opcode::If) {
          AddSuccessor(top.parent, top.next, "F");
        }
        StartBasicBlock(top.next, ptr);
        break;
      }

      case Opcode::Br:
        Br(instr.index_immediate());
        MarkUnreachable(ptr);
        break;

      case Opcode::BrIf: {
        Br(instr.index_immediate(), "T");
        auto next = NewBasicBlock();
        AddSuccessor(next, "F");
        StartBasicBlock(next, ptr);
        break;
      }

      case Opcode::BrTable: {
        const auto& immediate = instr.br_table_immediate();
        u32 value = 0;
        for (const auto& target : immediate.targets) {
          Br(target, format("{}", value++));
        }
        Br(immediate.default_target, "default");
        MarkUnreachable(ptr);
        break;
      }

      case Opcode::Return:
      case Opcode::ReturnCall:
      case Opcode::ReturnCallIndirect:
        MarkUnreachable(ptr);
        break;

      default:
        break;
    }
  }
}

void Tool::RemoveEmptyBasicBlocks() {
  std::map<BBID, BBID> empty_map;
  // Map each empty bb to its successor.
  BBID cur = 0;
  for (const auto& bb: cfg) {
    if (bb.empty()) {
      BBID next = bb.successors.empty() ? InvalidBBID : bb.successors[0].bbid;
      empty_map.emplace(cur, next);
    }
    ++cur;
  }

  for (auto& pair: empty_map) {
    // Follow the chain of empty bbs to the first non-empty one (or
    // InvalidBBID).
    BBID last = pair.second;
    auto it = empty_map.find(last);
    if (it != empty_map.end()) {
      while (it != empty_map.end()) {
        last = it->second;
        it = empty_map.find(last);
      }

      // Replace all successors in the chain with `last`.
      BBID next = pair.second;
      pair.second = last;
      it = empty_map.find(next);
      while (it != empty_map.end()) {
        next = it->second;
        it->second = last;
        it = empty_map.find(next);
      }
    }
  }

  // Update all non-empty bbs that have empty successors.
  for (auto& bb: cfg) {
    if (!bb.empty()) {
      for (auto& succ: bb.successors) {
        auto it = empty_map.find(succ.bbid);
        if (it != empty_map.end()) {
          succ.bbid = it->second;
        }
      }
    }
  }
}

namespace {

std::string EscapeString(string_view s) {
  std::string result;
  for (char c: s) {
    switch (c) {
      case '{':
      case '}':
        result += '\\';
        // Fallthrough.

      default:
        result += c;
        break;
    }
  }
  return result;
}

bool IsExtraneousInstruction(const Instruction& instr) {
  auto opcode = instr.opcode;
  return opcode == Opcode::Block || opcode == Opcode::Else ||
         opcode == Opcode::End || opcode == Opcode::Br;
}

}  // namespace

void Tool::WriteDotFile() {
  const int kMaxSuccessors = 64;

  std::ofstream fstream;
  std::ostream* stream = &std::cout;
  if (!options.output_filename.empty()) {
    fstream = std::ofstream{options.output_filename.to_string()};
    if (fstream) {
      stream = &fstream;
    }
  }

  print(*stream, "strict digraph {{\n");

  // Write nodes.
  BBID bbid = 0;
  for (const auto& bb: cfg) {
    if (!bb.empty()) {
      print(*stream, "  {} [shape=record;label=\"{{", bbid);
      auto instrs = ReadExpression(bb.code, options.features, errors);
      for (const auto& instr: instrs) {
        if (IsExtraneousInstruction(instr)) {
          continue;
        } else if (instr.opcode == Opcode::BrTable) {
          print(*stream, "{}...\\l", EscapeString(format("{}", instr.opcode)));
        } else {
          print(*stream, "{}\\l", EscapeString(format("{}", instr)));
        }
      }
      // Add ports.
      if (bb.successors.size() > 1) {
        print(*stream, "|{{");
        string_view sep = "";
        int count = 0;
        for (const auto& succ: bb.successors) {
          if (count < kMaxSuccessors) {
            assert(!succ.name.empty());
            print(*stream, "{}<{}>{}", sep, succ.name, succ.name);
          } else {
            print(*stream, "{}<trunc> ...", sep);
            break;
          }
          sep = "|";
          ++count;
        }
        print(*stream, "}}");
      }
      print(*stream, "}}\"]\n");
    }
    ++bbid;
  }

  // Write edges.
  print(*stream, "  start -> {}\n", start_bbid);
  bbid = 0;
  for (const auto& bb: cfg) {
    if (!bb.empty()) {
      int count = 0;
      for (const auto& succ : bb.successors) {
        if (succ.bbid == InvalidBBID) {
          print(*stream, "  {} -> end", bbid);
        } else {
          print(*stream, "  {}", bbid);
          if (!succ.name.empty()) {
            if (count < kMaxSuccessors) {
              print(*stream, ":{}", succ.name);
            } else {
              print(*stream, ":trunc");
            }
          }
          print(*stream, " -> {}", succ.bbid);
          if (count >= kMaxSuccessors && !succ.name.empty()) {
            print(*stream, " [headlabel=\"{}\"]", succ.name);
          }
          print(*stream, "\n");
        }
        ++count;
      }
    }
    ++bbid;
  }

  print(*stream, "}}\n");
  stream->flush();
}

void Tool::InsertFunctionName(Index index, string_view name) {
  name_to_function.emplace(name, index);
}

void Tool::PushLabel(Opcode opcode, BBID br, BBID next) {
  labels.push_back({opcode, current_bbid, br, next});
}

Label Tool::PopLabel() {
  assert(!labels.empty());
  Label top = labels.back();
  labels.pop_back();
  return top;
}

BBID Tool::NewBasicBlock() {
  return next_bbid++;
}

BasicBlock& Tool::GetBasicBlock(BBID bbid) {
  if (bbid >= cfg.size()) {
    cfg.resize(bbid + 1);
  }
  return cfg[bbid];
}

void Tool::StartBasicBlock(BBID bbid, const u8* start) {
  if (current_bbid != InvalidBBID) {
    EndBasicBlock(start);
  }
  current_bbid = bbid;
  if (current_bbid != InvalidBBID) {
    GetBasicBlock(current_bbid).code = SpanU8{start, start};
  }
}

void Tool::EndBasicBlock(const u8* end) {
  auto& bb = GetBasicBlock(current_bbid);
  const u8* start = bb.code.data();
  bb.code = SpanU8{start, end};

  auto instrs = ReadExpression(bb.code, options.features, errors);
  if (std::all_of(instrs.begin(), instrs.end(), IsExtraneousInstruction)) {
    bb.code = SpanU8{};
  }
}

void Tool::MarkUnreachable(const u8* ptr) {
  StartBasicBlock(NewBasicBlock(), ptr);
}

void Tool::AddSuccessor(BBID bbid, const std::string& name) {
  AddSuccessor(current_bbid, bbid, name);
}

void Tool::AddSuccessor(BBID from, BBID to, const std::string& name) {
  GetBasicBlock(from).successors.push_back({name, to});
}

void Tool::Br(Index index, const std::string& name) {
  if (index < labels.size()) {
    AddSuccessor(labels[labels.size() - index - 1].br, name);
  } else {
    print("Invalid branch depth: {}\n", index);
  }
}

}  // namespace cfg
}  // namespace tools
}  // namespace wasp