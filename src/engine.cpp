#include "orbit/engine.hpp"

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace orbit {
namespace {
struct Context {
    Limits limits;
    std::size_t used = 0;
    std::string output;
    std::chrono::steady_clock::time_point deadline;
    bool timed_out = false;
};

void* allocate(void* userdata, void* pointer, std::size_t old_size, std::size_t new_size) {
    auto& context = *static_cast<Context*>(userdata);
    const auto old = pointer ? old_size : 0;
    if (!new_size) {
        context.used -= old;
        std::free(pointer);
        return nullptr;
    }
    if (new_size > context.limits.memory - (context.used - old))
        return nullptr;
    if (void* next = std::realloc(pointer, new_size)) {
        context.used = context.used - old + new_size;
        return next;
    }
    return nullptr;
}

Context& context(lua_State* state) {
    return *static_cast<Context*>(lua_callbacks(state)->userdata);
}

void append(lua_State* state, std::string_view text) {
    auto& c = context(state);
    if (text.size() > c.limits.output - c.output.size())
        luaL_error(state, "output limit exceeded");
    c.output.append(text);
}

int print(lua_State* state) {
    const int count = lua_gettop(state);
    for (int i = 1; i <= count; ++i) {
        if (i != 1)
            append(state, "\t");
        std::size_t size = 0;
        const char* value = luaL_tolstring(state, i, &size);
        append(state, {value, size});
        lua_pop(state, 1);
    }
    append(state, "\n");
    return 0;
}

void interrupt(lua_State* state, int gc) {
    // Luau disallows raising an error from the GC interrupt callback.
    if (gc < 0 && std::chrono::steady_clock::now() >= context(state).deadline) {
        context(state).timed_out = true;
        // A breakpoint yields to the embedding host; unlike a script error it
        // cannot be swallowed by pcall in a tight loop.
        if (lua_isyieldable(state)) lua_break(state);
        else luaL_error(state, "execution time limit exceeded");
    }
}

void append_error(Context& c, std::string_view error) {
    constexpr std::string_view prefix = "\nError: ";
    // Keep the entire IPC response bounded, including diagnostics.
    const std::size_t room = max_output - std::min(max_output, c.output.size());
    c.output.append(prefix.substr(0, room));
    c.output.append(error.substr(0, max_output - c.output.size()));
}
}

Result execute(std::string_view source, std::uint32_t host_pid, Limits limits) {
    if (source.size() > max_source)
        return {false, "Source exceeds the 256 KiB limit."};
    if (limits.memory < 256 * 1024 || limits.output > max_output || limits.time.count() <= 0)
        return {false, "Invalid runtime limits."};

    Context c{limits, 0, {}, {}};
    try {
        const std::string bytecode = Luau::compile(std::string(source));
        std::unique_ptr<lua_State, decltype(&lua_close)> state(lua_newstate(allocate, &c), lua_close);
        if (!state)
            return {false, "Could not create the Luau VM within the memory limit."};
        lua_callbacks(state.get())->userdata = &c;
        luaL_openlibs(state.get());
        lua_pushcfunction(state.get(), print, "print");
        lua_setglobal(state.get(), "print");
        lua_pushnumber(state.get(), host_pid);
        lua_setglobal(state.get(), "_HOST_PID");
        luaL_sandbox(state.get());
        lua_State* thread = lua_newthread(state.get());
        luaL_sandboxthread(thread);
        if (luau_load(thread, "=editor", bytecode.data(), bytecode.size(), 0) != 0) {
            append_error(c, lua_tostring(thread, -1));
            return {false, std::move(c.output)};
        }
        c.deadline = std::chrono::steady_clock::now() + limits.time;
        lua_callbacks(thread)->interrupt = interrupt;
        const int status = lua_resume(thread, nullptr, 0);
        const bool ok = status == LUA_OK && !c.timed_out;
        if (!ok) {
            const char* error = lua_gettop(thread) > 0 ? lua_tostring(thread, -1) : nullptr;
            append_error(c, c.timed_out ? "execution time limit exceeded" :
                           (error ? error : "Script yielded or failed without a message."));
        }
        return {ok, std::move(c.output)};
    } catch (const std::exception& error) {
        append_error(c, error.what());
        return {false, std::move(c.output)};
    }
}
}
