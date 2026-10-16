#pragma once
// The game's script machine: Squirrel 2.2.4 (third_party/squirrel2, the version the game embeds),
// running its compiled scripts (`script\*.nut`, docs/SCRIPTS.md) unchanged. The native side is the
// 24 tables Cr3BindNativeClosure gives the root table; functions not implemented yet are stubs that
// record their calls and return null.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct SQVM;

namespace cr2 {

// A native's call: its arguments (from 0, after `this`) and its result
class NativeCall {
public:
    NativeCall(SQVM* v, int count) : v_(v), count_(count) {}
    int count() const { return count_; }
    int integer(int i, int fallback = 0) const; // (floats and bools convert)
    float number(int i, float fallback = 0) const;
    bool boolean(int i, bool fallback = false) const;
    std::string string(int i) const;
    void result(int v);
    void result(float v);
    void result(bool v);
    void result(const std::string& v);
    bool hasResult() const { return result_; }

private:
    SQVM* v_;
    int count_;
    bool result_ = false;
};
using Native = std::function<void(NativeCall&)>;

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
    // an implementation for a native ("Table.Function", or a root function's name), in place of
    // its stub
    void setNative(const std::string& table, const std::string& function, Native fn);

    // stub calls so far: "Table.Function" -> count
    const std::map<std::string, int>& stubCalls() const { return stubCalls_; }
    // the native API's table and function names (docs/SCRIPTS.md), as bound
    static const std::vector<std::pair<std::string, std::vector<std::string>>>& nativeApi();

private:
    void bindNatives();
    SQVM* vm_ = nullptr;
    std::string error_;
    std::map<std::string, int> stubCalls_;
    std::deque<Native> natives_; // (their addresses are the closures' free variables)
    friend long stubNativeCalls(ScriptVM&, const char*);
};

} // namespace cr2
