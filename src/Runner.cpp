/**
 * @file Runner.cpp
 *
 * This module contains the implementation of the Runner class.
 *
 * © 2019 by Richard Walters
 */

#include "Runner.hpp"

#include <stdlib.h>
#include <string>
#include <SystemAbstractions/StringExtensions.hpp>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {

    /**
     * This function is provided to the Lua interpreter for use in
     * allocating memory.
     *
     * @param[in] ud
     *     This is the "ud" opaque pointer given to lua_newstate when
     *     the Lua interpreter state was created.
     *
     * @param[in] ptr
     *     If not NULL, this points to the memory block to be
     *     freed or reallocated.
     *
     * @param[in] osize
     *     This is the size of the memory block pointed to by "ptr".
     *
     * @param[in] nsize
     *     This is the number of bytes of memory to allocate or reallocate,
     *     or zero if the given block should be freed instead.
     *
     * @return
     *     A pointer to the allocated or reallocated memory block is
     *     returned, or NULL is returned if the given memory block was freed.
     */
    void* LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize) {
        if (nsize == 0) {
            free(ptr);
            return NULL;
        } else {
            return realloc(ptr, nsize);
        }
    }

    /**
     * This structure is used to pass state from the caller of the
     * lua_load function to the reader function supplied to lua_load.
     */
    struct LuaReaderState {
        /**
         * This is the code chunk to be read by the Lua interpreter.
         */
        const std::string* chunk = nullptr;

        /**
         * This flag indicates whether or not the Lua interpreter
         * has been fed the code chunk as input yet.
         */
        bool read = false;
    };

    /**
     * This function is provided to the Lua interpreter in order to
     * read the next chunk of code to be interpreted.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     *
     * @param[in] data
     *     This points to a LuaReaderState structure containing
     *     state information provided by the caller of lua_load.
     *
     * @param[out] size
     *     This points to where the size of the next chunk of code
     *     should be stored.
     *
     * @return
     *     A pointer to the next chunk of code to interpret is returned.
     *
     * @retval NULL
     *     This is returned once all the code to be interpreted has
     *     been read.
     */
    const char* LuaReader(lua_State* lua, void* data, size_t* size) {
        LuaReaderState* state = (LuaReaderState*)data;
        if (state->read) {
            return NULL;
        } else {
            state->read = true;
            *size = state->chunk->length();
            return state->chunk->c_str();
        }
    }

    /**
     * This function is provided to the Lua interpreter when lua_pcall
     * is called.  It is called by the Lua interpreter if a runtime
     * error occurs while interpreting scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     *
     * @return
     *     The number of return values that have been pushed onto the
     *     Lua stack by the function as return values of the function
     *     is returned.
     */
    int LuaTraceback(lua_State* lua) {
        const char* message = lua_tostring(lua, 1);
        if (message == NULL) {
            if (!lua_isnoneornil(lua, 1)) {
                if (!luaL_callmeta(lua, 1, "__tostring")) {
                    lua_pushliteral(lua, "(no error message)");
                }
            }
        } else {
            luaL_traceback(lua, lua, message, 1);
        }
        return 1;
    }

}

/**
 * This is the internal interface/implementation of the Runner class.
 */
struct Runner::Impl {
    /**
     * This points to the Lua interpreter that the runner is encapsulating.
     * This interpreter is used to execute the Lua test scripts.
     */
    lua_State* lua = nullptr;

    Impl() {
        // Instantiate Lua interpreter.
        lua = lua_newstate(LuaAllocator, NULL);

        // Load standard Lua libraries.
        //
        // Temporarily disable the garbage collector as we load the
        // libraries, to improve performance
        // (http://lua-users.org/lists/lua-l/2008-07/msg00690.html).
        lua_gc(lua, LUA_GCSTOP, 0);
        luaL_openlibs(lua);
        lua_gc(lua, LUA_GCRESTART, 0);

        // Initialize wrapper types.
    }

    ~Impl() {
        lua_close(lua);
    }

    Impl(const Impl&) = delete;
    Impl(Impl&&) noexcept = default;
    Impl& operator=(const Impl&) = delete;
    Impl& operator=(Impl&&) noexcept = default;
};

Runner::~Runner() noexcept = default;
Runner::Runner(Runner&&) noexcept = default;
Runner& Runner::operator=(Runner&&) noexcept = default;

Runner::Runner()
    : impl_(new Impl())
{
}

std::string Runner::LoadScript(
    const std::string& name,
    const std::string& script
) {
    lua_settop(impl_->lua, 0);
    lua_pushcfunction(impl_->lua, LuaTraceback);
    LuaReaderState luaReaderState;
    luaReaderState.chunk = &script;
    std::string errorMessage;
    switch (const int luaLoadResult = lua_load(impl_->lua, LuaReader, &luaReaderState, ("=" + name).c_str(), "t")) {
        case LUA_OK: {
            const int luaPCallResult = lua_pcall(impl_->lua, 0, 0, 1);
            if (luaPCallResult != LUA_OK) {
                if (!lua_isnil(impl_->lua, -1)) {
                    errorMessage = lua_tostring(impl_->lua, -1);
                }
            }
        } break;
        case LUA_ERRSYNTAX: {
            errorMessage = lua_tostring(impl_->lua, -1);
        } break;
        case LUA_ERRMEM: {
            errorMessage = "LUA_ERRMEM";
        } break;
        case LUA_ERRGCMM: {
            errorMessage = "LUA_ERRGCMM";
        } break;
        default: {
            errorMessage = SystemAbstractions::sprintf("(unexpected lua_load result: %d)", luaLoadResult);
        } break;
    }
    lua_settop(impl_->lua, 0);
    return errorMessage;
}

std::vector< std::string > Runner::GetTestNames() const {
    std::vector< std::string > testNames;
    lua_getglobal(impl_->lua, "_G");
    lua_pushnil(impl_->lua);
    while (lua_next(impl_->lua, -2) != 0) {
        static const std::string testPrefix = "test_";
        static const size_t testPrefixLength = testPrefix.length();
        const std::string key = luaL_checkstring(impl_->lua, -2);
        if (
            (key.length() >= testPrefixLength)
            && (key.substr(0, testPrefixLength) == testPrefix)
        ) {
            testNames.push_back(std::move(key));
        }
        lua_pop(impl_->lua, 1);
    }
    lua_pop(impl_->lua, 1);
    return testNames;
}
