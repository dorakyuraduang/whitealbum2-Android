#pragma once
#include "winrecomp/image.hpp"
#include <Zydis/Zydis.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace wr {
enum class Flow { normal, call, jump, conditional, ret, stop };
struct Instruction {
    Address address{};
    ZydisDecodedInstruction decoded{};
    std::array<ZydisDecodedOperand,ZYDIS_MAX_OPERAND_COUNT> operands{};
    std::vector<std::uint8_t> bytes;
    std::string text;
    Flow flow{Flow::normal};
    std::optional<Address> target, absolute_slot;
    Address next() const { return address + decoded.length; }
};
class Decoder {
    ZydisDecoder decoder_{};
    ZydisFormatter formatter_{};
public:
    Decoder();
    std::optional<Instruction> decode(std::span<const std::uint8_t> bytes, Address address) const;
};
struct Edge { Address from{}; std::optional<Address> to; std::string kind, detail; };
struct Block { Address start{}, end{}; std::vector<Address> instructions; };
struct JumpTable { Address branch{}, guard{}, table{}; std::vector<Address> targets; };
struct Graph {
    Address entry{};
    std::map<Address,Instruction> instructions;
    std::map<Address,Block> blocks;
    std::set<Address> function_candidates, roots;
    std::vector<Edge> edges;
    std::vector<std::string> diagnostics;
    std::vector<Address> jump_table_candidates;
    std::vector<JumpTable> recovered_tables;
    bool budget_exhausted{};
    std::string json(const Image& image) const;
};
Graph discover(const Image& image, Address entry, std::size_t budget=1000000, bool include_image_roots=true,const std::set<Address>& extra_roots={});
std::string emit_cpp(const Graph& graph, const std::string& name="recompiled",bool partial=false);
std::string coverage_json(const Graph& graph);
void emit_project(const Image& image,const Graph& graph,const std::filesystem::path& output,bool partial=false);
std::string hex(std::uint64_t value);
std::string quote(const std::string& value);
void write_file(const std::filesystem::path& path, const std::string& content);
}
