#include "script/vm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "squirrel.h"
#include "sqstdaux.h"
#include "sqstdblob.h"
#include "sqstdio.h"

namespace cr2 {

namespace {

// Cr3BindNativeClosure's tables (the Android library: tools/sq_bindings.py, gen_native_api.py)
const std::vector<std::pair<std::string, std::vector<std::string>>> kNativeApi = {
#include "script/native_api.inc"
};

// Cr3BindVectorStruct's classes (Sqrat, a default constructor and float members)
const char kVectorClasses[] =
    "class Vector3 { x = 0.0; y = 0.0; z = 0.0; }\n"
    "class Vector4 { x = 0.0; y = 0.0; z = 0.0; w = 0.0; }\n";

// The game's scripts were compiled by a 32-bit Squirrel: every SQInteger in the stream (tags,
// counts, string lengths, integer literals, outer / local variable fields, line infos, default
// parameters, the stack size) is 4 bytes. Squirrel 2.2.4 makes SQInteger 64-bit on LP64 / Win64
// hosts (_SQ64) and reads it at that width, so the stream is rewritten with each such field
// widened (sign-extended; object types, floats and instructions keep their sizes). The fields
// follow SQClosure::Load / SQFunctionProto::Load / ReadObject (sqobject.cpp).
class Widener {
public:
    Widener(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) : in_(in), out_(out) {}

    bool run() {
        if (!copy(2)) return false; // 0xfafa: an unsigned short
        if (!integer() || !integer()) return false; // 'SQIR', sizeof(SQChar)
        if (!proto()) return false;
        return integer() && p_ == in_.size(); // 'TAIL'
    }

private:
    bool copy(size_t n) {
        if (p_ + n > in_.size()) return false;
        out_.insert(out_.end(), in_.begin() + long(p_), in_.begin() + long(p_ + n));
        p_ += n;
        return true;
    }
    bool read32(int32_t& v) {
        if (p_ + 4 > in_.size()) return false;
        std::memcpy(&v, in_.data() + p_, 4);
        p_ += 4;
        return true;
    }
    bool integer(int32_t* value = nullptr) { // one SQInteger, 4 bytes in, sizeof(SQInteger) out
        int32_t v;
        if (!read32(v)) return false;
        const SQInteger w = v;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&w);
        out_.insert(out_.end(), b, b + sizeof w);
        if (value) *value = v;
        return true;
    }
    bool object() {
        int32_t type;
        if (!read32(type)) return false;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&type);
        out_.insert(out_.end(), b, b + 4); // SQObjectType
        switch (uint32_t(type)) {
        case OT_STRING: {
            int32_t len;
            return integer(&len) && len >= 0 && copy(size_t(len));
        }
        case OT_INTEGER: return integer();
        case OT_FLOAT: return copy(4);
        case OT_NULL: return true;
        default: return false;
        }
    }
    bool proto() {
        int32_t n[8]; // literals, parameters, outer values, locals, line infos, default params, instructions, functions
        if (!integer() || !object() || !object() || !integer()) return false; // PART, source, name, PART
        for (int32_t& k : n)
            if (!integer(&k) || k < 0) return false;
        if (!integer()) return false;
        for (int32_t i = 0; i < n[0]; ++i)
            if (!object()) return false;
        if (!integer()) return false;
        for (int32_t i = 0; i < n[1]; ++i)
            if (!object()) return false;
        if (!integer()) return false;
        for (int32_t i = 0; i < n[2]; ++i)
            if (!integer() || !object() || !object()) return false; // type, source, name
        if (!integer()) return false;
        for (int32_t i = 0; i < n[3]; ++i)
            if (!object() || !integer() || !integer() || !integer()) return false; // name, pos, start op, end op
        if (!integer()) return false;
        for (int32_t i = 0; i < 2 * n[4]; ++i)
            if (!integer()) return false; // SQLineInfo: line, op
        if (!integer()) return false;
        for (int32_t i = 0; i < n[5]; ++i)
            if (!integer()) return false;
        if (!integer() || !copy(8 * size_t(n[6]))) return false; // SQInstruction: always 8 bytes
        if (!integer()) return false;
        for (int32_t i = 0; i < n[7]; ++i)
            if (!proto()) return false;
        return integer() && copy(2); // stack size; generator, varparams (bools)
    }

    const std::vector<uint8_t>& in_;
    std::vector<uint8_t>& out_;
    size_t p_ = 0;
};

struct MemReader {
    const uint8_t* data;
    size_t size, pos;
};

SQInteger readBytes(SQUserPointer up, SQUserPointer dest, SQInteger n) {
    MemReader* r = static_cast<MemReader*>(up);
    const size_t k = n < 0 ? 0 : std::min(size_t(n), r->size - r->pos);
    std::memcpy(dest, r->data + r->pos, k);
    r->pos += k;
    return SQInteger(k) == n ? n : -1;
}

ScriptVM* owner(HSQUIRRELVM v) { return static_cast<ScriptVM*>(sq_getforeignptr(v)); }

} // namespace

long stubNativeCalls(ScriptVM& s, const char* name) { return ++s.stubCalls_[name]; }

namespace {

void scriptError(HSQUIRRELVM v, const SQChar* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (ScriptVM* s = owner(v)) s->appendError(buf);
}

// A native not implemented yet: its "Table.Function" is the closure's free variable (pushed
// after the arguments in Squirrel 2); counts the call, returns null
SQInteger stubNative(HSQUIRRELVM v) {
    const SQChar* name = nullptr;
    if (SQ_SUCCEEDED(sq_getstring(v, sq_gettop(v), &name)) && name)
        if (ScriptVM* s = owner(v))
            if (stubNativeCalls(*s, name) > 100000) // (a script waiting on a stub forever)
                return sq_throwerror(v, (std::string("stub called 100000 times: ") + name).c_str());
    return 0;
}

// An implemented native: the Native's address is the closure's free variable (on the stack's top)
SQInteger implementedNative(HSQUIRRELVM v) {
    SQUserPointer p = nullptr;
    if (SQ_FAILED(sq_getuserpointer(v, sq_gettop(v), &p)) || !p) return 0;
    NativeCall call(v, int(sq_gettop(v)) - 2); // (1 this, 2.. the arguments, the top the free variable)
    (*static_cast<Native*>(p))(call);
    return call.hasResult() ? 1 : 0;
}

} // namespace

int NativeCall::integer(int i, int fallback) const {
    if (i >= count_) return fallback;
    SQInteger n;
    SQFloat f;
    SQBool b;
    if (SQ_SUCCEEDED(sq_getinteger(v_, i + 2, &n))) return int(n);
    if (SQ_SUCCEEDED(sq_getfloat(v_, i + 2, &f))) return int(f);
    if (SQ_SUCCEEDED(sq_getbool(v_, i + 2, &b))) return b ? 1 : 0;
    return fallback;
}

float NativeCall::number(int i, float fallback) const {
    if (i >= count_) return fallback;
    SQFloat f;
    SQInteger n;
    if (SQ_SUCCEEDED(sq_getfloat(v_, i + 2, &f))) return float(f);
    if (SQ_SUCCEEDED(sq_getinteger(v_, i + 2, &n))) return float(n);
    return fallback;
}

bool NativeCall::boolean(int i, bool fallback) const {
    if (i >= count_) return fallback;
    SQBool b;
    SQInteger n;
    if (SQ_SUCCEEDED(sq_getbool(v_, i + 2, &b))) return b != 0;
    if (SQ_SUCCEEDED(sq_getinteger(v_, i + 2, &n))) return n != 0;
    return fallback;
}

std::string NativeCall::string(int i) const {
    const SQChar* s = nullptr;
    if (i < count_ && SQ_SUCCEEDED(sq_getstring(v_, i + 2, &s)) && s) return s;
    return "";
}

void NativeCall::result(int v) { sq_pushinteger(v_, v), result_ = true; }
void NativeCall::result(float v) { sq_pushfloat(v_, v), result_ = true; }
void NativeCall::result(bool v) { sq_pushbool(v_, v ? SQTrue : SQFalse), result_ = true; }
void NativeCall::result(const std::string& v) { sq_pushstring(v_, v.c_str(), -1), result_ = true; }

void ScriptVM::setNative(const std::string& table, const std::string& function, Native fn) {
    natives_.push_back(std::move(fn));
    sq_pushroottable(vm_);
    if (!table.empty()) {
        sq_pushstring(vm_, table.c_str(), -1);
        if (SQ_FAILED(sq_get(vm_, -2))) {
            sq_pushstring(vm_, table.c_str(), -1);
            sq_newtable(vm_);
            sq_newslot(vm_, -3, SQFalse);
            sq_pushstring(vm_, table.c_str(), -1);
            sq_get(vm_, -2);
        }
    }
    sq_pushstring(vm_, function.c_str(), -1);
    sq_pushuserpointer(vm_, &natives_.back());
    sq_newclosure(vm_, implementedNative, 1);
    sq_createslot(vm_, -3);
    sq_pop(vm_, table.empty() ? 1 : 2);
}

const std::vector<std::pair<std::string, std::vector<std::string>>>& ScriptVM::nativeApi() { return kNativeApi; }

ScriptVM::ScriptVM() {
    vm_ = sq_open(1024);
    sq_setforeignptr(vm_, this);
    sq_setprintfunc(vm_, scriptError);
    sq_pushroottable(vm_);
    sqstd_register_bloblib(vm_); // (the game registers these two)
    sqstd_register_iolib(vm_);
    sq_pop(vm_, 1);
    sqstd_seterrorhandlers(vm_);
    bindNatives();
}

ScriptVM::~ScriptVM() {
    if (vm_) sq_close(vm_);
}

void ScriptVM::bindNatives() {
    sq_pushroottable(vm_);
    for (const auto& [table, functions] : kNativeApi) {
        sq_pushstring(vm_, table.c_str(), -1);
        sq_newtable(vm_);
        for (const std::string& f : functions) {
            sq_pushstring(vm_, f.c_str(), -1);
            sq_pushstring(vm_, (table + "." + f).c_str(), -1);
            sq_newclosure(vm_, stubNative, 1);
            sq_newslot(vm_, -3, SQFalse);
        }
        sq_newslot(vm_, -3, SQFalse);
    }
    // BindNativeClosure's one root function: WaitFrame(n) - the script waits n frames
    // (ScriptLib state 3, then sq_suspendvm; the manager wakes it). A stub for now
    sq_pushstring(vm_, "WaitFrame", -1);
    sq_pushstring(vm_, "WaitFrame", -1);
    sq_newclosure(vm_, stubNative, 1);
    sq_newslot(vm_, -3, SQFalse);
    sq_pop(vm_, 1);
    if (SQ_SUCCEEDED(sq_compilebuffer(vm_, kVectorClasses, SQInteger(sizeof kVectorClasses - 1), "vector", SQTrue))) {
        sq_pushroottable(vm_);
        sq_call(vm_, 1, SQFalse, SQTrue);
        sq_pop(vm_, 1);
    }
}

bool ScriptVM::load(const std::vector<uint8_t>& bytecode, const std::string& name) {
    error_.clear();
    std::vector<uint8_t> wide;
    const std::vector<uint8_t>* stream = &bytecode;
    if (sizeof(SQInteger) != 4) {
        wide.reserve(bytecode.size() * 3 / 2);
        if (!Widener(bytecode, wide).run()) {
            error_ = name + ": not a 32-bit Squirrel 2 script";
            return false;
        }
        stream = &wide;
    }
    MemReader r{stream->data(), stream->size(), 0};
    if (SQ_FAILED(sq_readclosure(vm_, readBytes, &r))) {
        error_ = name + ": not loadable" + (error_.empty() ? "" : ": " + error_);
        return false;
    }
    return true;
}

bool ScriptVM::runMain() {
    sq_pushroottable(vm_);
    const bool ok = SQ_SUCCEEDED(sq_call(vm_, 1, SQFalse, SQTrue));
    sq_pop(vm_, 1); // the closure
    return ok;
}

bool ScriptVM::hasFunction(const std::string& function) const {
    sq_pushroottable(vm_);
    sq_pushstring(vm_, function.c_str(), -1);
    const bool found = SQ_SUCCEEDED(sq_get(vm_, -2)) && sq_gettype(vm_, -1) == OT_CLOSURE;
    sq_settop(vm_, sq_gettop(vm_) - (found ? 2 : 1));
    return found;
}

std::string ScriptVM::findFunction(const std::string& name) const {
    const auto lower = [](std::string t) {
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const std::string want = lower(name);
    std::string found;
    const SQInteger top = sq_gettop(vm_);
    sq_pushroottable(vm_);
    sq_pushnull(vm_);
    while (found.empty() && SQ_SUCCEEDED(sq_next(vm_, -2))) { // key at -2, value at -1
        const SQChar* k = nullptr;
        if (sq_gettype(vm_, -1) == OT_CLOSURE && SQ_SUCCEEDED(sq_getstring(vm_, -2, &k)) && k && lower(k) == want) found = k;
        sq_pop(vm_, 2);
    }
    sq_settop(vm_, top);
    return found;
}

bool ScriptVM::call(const std::string& function, const std::vector<int>& args) {
    const SQInteger top = sq_gettop(vm_);
    sq_pushroottable(vm_);
    sq_pushstring(vm_, function.c_str(), -1);
    if (SQ_FAILED(sq_get(vm_, -2))) {
        sq_settop(vm_, top);
        error_ = function + ": no such function";
        return false;
    }
    sq_pushroottable(vm_);
    for (int a : args) sq_pushinteger(vm_, a);
    const bool ok = SQ_SUCCEEDED(sq_call(vm_, SQInteger(args.size()) + 1, SQFalse, SQTrue));
    sq_settop(vm_, top);
    return ok;
}

} // namespace cr2
