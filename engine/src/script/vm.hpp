#pragma once
// The game's script machine: Squirrel 2.2.4 (third_party/squirrel2, the version the game embeds),
// running its compiled scripts (`script\*.nut`, docs/SCRIPTS.md) unchanged. The native side is the
// 24 tables Cr3BindNativeClosure gives the root table; functions not implemented yet are stubs that
// record their calls and return null.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct SQVM;

namespace cr2 {

class ScriptVM {
public:
    ScriptVM();
    ~ScriptVM();
    ScriptVM(const ScriptVM&) = delete;
    ScriptVM& operator=(const ScriptVM&) = delete;

    SQVM* vm() const { return vm_; }
    // a compiled script into a closure on the stack's top (false: not loadable, error() says why)
    bool load(const std::vector<uint8_t>& bytecode, const std::string& name);
    // runs the closure load() left (its `main`: it defines the script's functions in the root table)
    bool runMain();
    // calls a root-table function by name with integer arguments
    bool call(const std::string& function, const std::vector<int>& args = {});
    bool hasFunction(const std::string& function) const;
    // a root-table function's name matching `name` but for case ("" when none)
    std::string findFunction(const std::string& name) const;

    const std::string& error() const { return error_; }
    void appendError(const std::string& text) { error_ += text; }
    // stub calls so far: "Table.Function" -> count
    const std::map<std::string, int>& stubCalls() const { return stubCalls_; }
    // the native API's table and function names (docs/SCRIPTS.md), as bound
    static const std::vector<std::pair<std::string, std::vector<std::string>>>& nativeApi();

private:
    void bindNatives();
    SQVM* vm_ = nullptr;
    std::string error_;
    std::map<std::string, int> stubCalls_;
    friend long stubNativeCalls(ScriptVM&, const char*);
};

} // namespace cr2
