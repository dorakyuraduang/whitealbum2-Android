#pragma once
#include "resource_archive.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wa2::script {
// This is the game's LSCR language, not an instruction-set/CPU interpreter.
// No guest registers, host APIs, machine addresses or x86 helpers enter here.
class Error : public std::runtime_error { using std::runtime_error::runtime_error; };
class Unsupported : public Error {using Error::Error;};
enum class Type : std::uint32_t { byte=1, short_integer=2, integer=3, real=4, text=5 };
class Value {
public:
    Value()=default;
    Value(std::int32_t value):data_(value){}
    Value(float value):data_(value){}
    Value(std::string value):data_(std::move(value)){}
    Type type() const noexcept;
    std::int32_t integer() const;
    float real() const;
    const std::string& text() const;
    static Value default_text();
    bool uses_default_text() const noexcept {return default_text_;}
    bool operator==(const Value&) const=default;
private:
    std::variant<std::int32_t,float,std::string> data_{std::int32_t(0)};
    bool default_text_{};
};
struct Variable { std::string name; Type storage{Type::integer}; Value value; };
class Globals {
public:
    static Globals parse(std::span<const std::uint8_t> bytes);
    std::span<const Variable> variables() const noexcept {return variables_;}
    const Value& get(std::uint32_t index) const;
    void set(std::uint32_t index,const Value& value);
private:
    std::vector<Variable> variables_;
};
struct Function {
    std::uint32_t index{};
    std::string name;
    Type result{Type::integer};
    std::vector<std::uint8_t> parameters;
    std::vector<std::string> default_text;
};
class Functions {
public:
    void load(std::span<const std::uint8_t> bytes);
    const Function& at(std::uint32_t index) const;
    std::size_t size() const noexcept {return functions_.size();}
private:
    std::vector<std::optional<Function>> functions_;
};
class Program {
public:
    struct Label {std::int32_t id{};std::uint32_t offset{};};
    static Program parse(std::string name,std::vector<std::uint8_t> binary,
                         std::span<const std::uint8_t> text);
    const std::string& name() const noexcept {return name_;}
    std::uint32_t version() const noexcept {return version_;}
    std::span<const Label> labels() const noexcept {return labels_;}
    std::span<const std::string> strings() const noexcept {return strings_;}
    std::uint32_t label(std::int32_t id) const;
    std::uint32_t word(std::uint32_t offset) const;
    std::size_t size() const noexcept {return binary_.size();}
private:
    std::string name_;
    std::uint32_t version_{};
    std::vector<std::uint8_t> binary_;
    std::vector<Label> labels_;
    std::vector<std::string> strings_;
};
class Library {
public:
    explicit Library(resource::Archive archive);
    std::shared_ptr<const Program> load(std::string_view scene) const;
    Globals& globals() noexcept {return globals_;}
    const Functions& functions() const noexcept {return functions_;}
private:
    resource::Archive archive_;
    Globals globals_;
    Functions functions_;
    std::vector<std::uint8_t> member(std::string_view name) const;
};
struct Command {
    std::uint32_t index{};
    std::span<const Value> arguments;
    std::uint32_t continuation{};
    std::int32_t caller{};
};
struct CommandResult {
    bool complete{true};
    bool yield_frame{true};
    std::optional<Value> value;
};
using Host=std::function<CommandResult(Command&)>;
enum class RunState { stopped, yielded, slice_exhausted };
class Runtime {
public:
    Runtime(std::shared_ptr<const Program> program,const Functions& functions,Globals& globals);
    bool start(std::int32_t label,std::int32_t caller=0);
    RunState advance(const Host& host,std::uint32_t maximum_operations=4096);
    std::uint32_t offset() const noexcept {return offset_;}
    std::size_t stack_depth() const noexcept {return depth_;}
    bool running() const noexcept {return running_;}
    const Program& program() const noexcept {return *program_;}
    std::uint64_t operations() const noexcept {return operations_;}
private:
    struct Slot {
        std::uint32_t kind{};
        std::int32_t local_index{-1};
        std::uint32_t reference{};
        Value value;
    };
    struct Flow {
        std::uint32_t kind{},end{},alternate{},again{},begin{};
        std::int32_t condition{};
        std::vector<std::pair<std::uint32_t,std::int32_t>> cases;
    };
    struct Pending {
        std::uint32_t index{},continuation{};
        std::vector<Value> arguments;
    };
    std::shared_ptr<const Program> program_;
    const Functions& functions_;
    Globals& globals_;
    std::array<Slot,256> stack_{};
    std::array<std::int32_t,26*16> integers_{};
    std::array<float,26*16> reals_{};
    std::array<Flow,16> flows_{};
    std::size_t depth_{},flow_depth_{};
    std::optional<Pending> pending_;
    std::uint32_t offset_{};
    std::int32_t caller_{};
    bool running_{};
    std::uint64_t operations_{};
    std::uint32_t read();
    void jump(std::uint32_t offset);
    void push(Slot value);
    void push_value(Value value);
    Value pop();
    void assign(Value value,bool restore_reference);
    bool control();
    void expression();
    bool command(const Host& host);
    void close_flows();
};
}
