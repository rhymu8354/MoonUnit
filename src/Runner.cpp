/**
 * @file Runner.cpp
 *
 * This module contains the implementation of the Runner class.
 *
 * © 2019 by Richard Walters
 */

#include "Runner.hpp"

#include <Json/Value.hpp>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <StringExtensions/StringExtensions.hpp>
#include <unordered_map>
#include <vector>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {

    /**
     * This is the file extension expected for Lua script files.
     */
    static const std::string luaFileExtension = ".lua";

    /**
     * This is the length of the file extension expected for Lua script files.
     */
    static const auto luaFileExtensionLength = luaFileExtension.length();

    /**
     * This holds information needed to run or report about a Lua test.
     */
    struct Test {
        /**
         * This is the Lua script to execute to prepare
         * the Lua interpreter for running the test.
         */
        std::string file;

        /**
         * This is the path to the file from which the Lua script was loaded.
         */
        std::string filePath;

        /**
         * This is the line number where the test was defined in the Lua
         * script from which the test was loaded.
         */
        int lineNumber = 0;
    };

    /**
     * This holds information about a suite of Lua tests found by the runner.
     */
    struct TestSuite {
        /**
         * This is the collection of tests recognized to be a part of the
         * suite.
         */
        std::unordered_map< std::string, Test > tests;
    };

    /**
     * This is used to handle the collection of test suites known by the test
     * runner.
     */
    using TestSuites = std::unordered_map< std::string, TestSuite >;

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

    /**
     * Push a value onto the Lua stack based on the given Json::Value.
     *
     * @param[in] lua
     *     This points to the Lua interpreter instance.
     *
     * @param[in] value
     *     This is the value that will be unwrapped from the given JSON value.
     */
    void LuaValueFromJsonValue(lua_State* lua, const Json::Value& value) {
        switch (value.GetType()) {
            case Json::Value::Type::Array:
            case Json::Value::Type::Object: {
                auto returnValue = (Json::Value*)lua_newuserdata(lua, sizeof(Json::Value));
                new (returnValue) Json::Value(value);
                luaL_setmetatable(lua, "json");
            } break;
            case Json::Value::Type::Boolean: {
                lua_pushboolean(lua, (bool)value ? 1 : 0);
            } break;
            case Json::Value::Type::FloatingPoint: {
                lua_pushnumber(lua, (lua_Number)(double)value);
            } break;
            case Json::Value::Type::Integer: {
                lua_pushinteger(lua, (lua_Integer)(int)value);
            } break;
            case Json::Value::Type::String: {
                const auto valueAsString = (std::string)value;
                lua_pushlstring(lua, valueAsString.c_str(), valueAsString.length());
            } break;
            case Json::Value::Type::Null:
            case Json::Value::Type::Invalid:
            default: {
                lua_pushnil(lua);
            }
        }
    }

    /**
     * Construct a new Json::Value based on a value on the Lua stack.
     *
     * @param[in] lua
     *     This points to the Lua interpreter instance.
     *
     * @param[in] index
     *     This is the index of the value on the Lua stack that will be wrapped.
     *
     * @return
     *     The newly constructed Json::Value is returned.
     */
    Json::Value JsonValueFromLuaValue(
        lua_State* lua,
        int index
    ) {
        luaL_checkany(lua, index);
        switch (lua_type(lua, index)) {
            case LUA_TNIL: {
                return Json::Value(nullptr);
            } break;
            case LUA_TNUMBER: {
                if (lua_isinteger(lua, index)) {
                    return Json::Value((int)lua_tointeger(lua, index));
                } else {
                    return Json::Value((double)lua_tonumber(lua, index));
                }
            } break;
            case LUA_TBOOLEAN: {
                return Json::Value(lua_toboolean(lua, index) != 0);
            } break;
            case LUA_TSTRING: {
                return Json::Value(lua_tostring(lua, index));
            } break;
            default: {
                (void)luaL_error(lua, "cannot construct a JSON value from a %s", lua_typename(lua, lua_type(lua, index)));
            }
        }
        return Json::Value();
    }

    /**
     * Extract the set of keys from the table at the given position in the Lua
     * stack.
     *
     * @param[in] lua
     *     This points to the Lua interpreter instance.
     *
     * @param[in] tableIndex
     *     This is the index on the Lua stack where the table can be found.
     *
     * @return
     *     The set of the keys from the Lua table is returned.
     */
    std::set< Json::Value > EnumerateKeys(lua_State* lua, int tableIndex) {
        std::set< Json::Value > keys;
        if (tableIndex < 0) {
            tableIndex = lua_gettop(lua) + tableIndex + 1;
        }
        lua_pushnil(lua);  // -1 = key
        while (lua_next(lua, tableIndex) != 0) { // -1 = value, -2 = key
            (void)keys.insert(JsonValueFromLuaValue(lua, -2));
            lua_pop(lua, 1); // -1 = key
        } // (stack empty)
        return keys;
    }

    /**
     * Perform a "deep" comparison between two Lua tables.
     *
     * @param[in] lua
     *     This points to the Lua interpreter instance.
     *
     * @param[in] lhsIndex
     *     This is the index of the first table on the Lua stack.
     *
     * @param[in] rhsIndex
     *     This is the index of the second table on the Lua stack.
     *
     * @param[in,out] keyChain
     *     This is where the "path" through the table structure is
     *     recorded, in the event that some difference is found.
     *
     * @return
     *     If the two tables are identical, an empty string is returned.
     *     Otherwise, a human-readable description of the mismatch
     *     is returned.
     */
    std::string CompareLuaTables(
        lua_State* lua,
        int lhsIndex,
        int rhsIndex,
        std::vector< Json::Value >& keyChain
    ) {
        if (lhsIndex < 0) {
            lhsIndex = lua_gettop(lua) + lhsIndex + 1;
        }
        if (rhsIndex < 0) {
            rhsIndex = lua_gettop(lua) + rhsIndex + 1;
        }
        const auto lhsKeys = EnumerateKeys(lua, lhsIndex);
        auto rhsKeys = EnumerateKeys(lua, rhsIndex);
        for (const auto& lhsKey: lhsKeys) {
            const auto rhsKeysEntry = rhsKeys.find(lhsKey);
            if (rhsKeysEntry == rhsKeys.end()) {
                return StringExtensions::sprintf(
                    "Actual value missing key '%s'",
                    lhsKey.ToEncoding().c_str()
                );
            } else {
                /*
                 * When using the keys to look up values in the two tables,
                 * make sure to use the proper key types.  Keys for "array"
                 * tables are numbers, not strings.  In other words, a
                 * table that looks like this:
                 *   { "a", "b", "c" }
                 * Has these keys:  1, 2, 3
                 * NOT these keys:  "1", "2", "3".
                 */
                LuaValueFromJsonValue(lua, lhsKey); // -1 = key
                lua_pushvalue(lua, -1); // -1 = key, -2 = key
                lua_gettable(lua, lhsIndex); // -1 = value(lhs), -2 = key
                const auto lhsType = lua_typename(lua, -1);
                lua_insert(lua, -2); // -1 = key, -2 = value(lhs)
                lua_gettable(lua, rhsIndex); // -1 = value(rhs), -2 = value(lhs)
                const auto rhsType = lua_typename(lua, -2);
                std::string comparisonResult;
                if (
                    lua_istable(lua, -1)
                    && lua_istable(lua, -2)
                ) {
                    keyChain.push_back(lhsKey);
                    comparisonResult = CompareLuaTables(lua, -2, -1, keyChain);
                    if (comparisonResult.empty()) {
                        keyChain.pop_back();
                    }
                } else if (lua_compare(lua, -1, -2, LUA_OPEQ) == 0) {
                    keyChain.push_back(lhsKey);
                    comparisonResult = StringExtensions::sprintf(
                        "Expected '%s', actual was '%s'\n",
                        lua_tostring(lua, -2),
                        lua_tostring(lua, -1)
                    );
                }
                lua_pop(lua, 2); // (stack empty)
                if (!comparisonResult.empty()) {
                    return comparisonResult;
                }
                (void)rhsKeys.erase(rhsKeysEntry);
            }
        }
        if (rhsKeys.empty()) {
            return "";
        } else {
            return StringExtensions::sprintf(
                "Actual value has extra key '%s'",
                rhsKeys.begin()->ToEncoding().c_str()
            );
        }
    }

    /**
     * Return the path to the parent folder containing the file or directory
     * at the given path.
     *
     * @param[in] path
     *     This is the path to the file or folder whose parent folder's
     *     path is to be returned.
     *
     * @return
     *     The path to the parent folder containing the file or directory
     *     at the given path is returned.
     */
    std::string ParentFolderPath(const std::string& path) {
        const auto delimiterIndex = path.find_last_of('/');
        if (delimiterIndex == std::string::npos) {
            return path;
        } else {
            return path.substr(0, delimiterIndex);
        }
    }

}

/**
 * This is the internal interface/implementation of the Runner class.
 */
struct Runner::Impl {
    // Properties

    /**
     * This points to the Lua interpreter that the runner is encapsulating.
     * This interpreter is used to execute the Lua test scripts.
     */
    lua_State* lua = nullptr;

    /**
     * This is where information about the test suites located by the
     * test runner are stored.
     */
    TestSuites testSuites;

    /**
     * This flag is set if any test expectation check fails.
     */
    bool currentTestFailed = false;

    /**
     * This is the function to call to report any errors in the current
     * test being run.
     */
    ErrorMessageDelegate errorMessageDelegate;

    /**
     * This is the Lua registry index of the table set up to hold Lua
     * objects associated with this instance.
     */
    int luaRegistryIndex = 0;

    // Lifecycle

    ~Impl() = default;
    Impl(const Impl&) = delete;
    Impl(Impl&&) noexcept = default;
    Impl& operator=(const Impl&) = delete;
    Impl& operator=(Impl&&) noexcept = default;

    // Methods

    /**
     * This is the constructor of the class.
     */
    Impl() = default;

    /**
     * Collect information about the test suites and tests which are registered
     * with the test runner via the moonunit.test (LuaTest) function.
     *
     * @param[in] file
     *     This is the contents of the Lua script file which was executed
     *     in order to register the test suites and tests.
     *
     * @param[in] filePath
     *     This is the path to the Lua script file which was executed
     *     in order to register the test suites and tests.
     */
    void FindTests(
        const std::string& file,
        const std::string& filePath
    ) {
        lua_rawgeti(lua, LUA_REGISTRYINDEX, luaRegistryIndex);
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            const std::string testSuiteName = luaL_checkstring(lua, -2);
            auto& testSuite = testSuites[testSuiteName];
            lua_pushnil(lua);
            while (lua_next(lua, -2) != 0) {
                const std::string testName = luaL_checkstring(lua, -2);
                lua_Debug debug;
                lua_getinfo(lua, ">S", &debug);
                Test test;
                test.file = file;
                test.filePath = filePath;
                test.lineNumber = debug.linedefined;
                testSuite.tests[testName] = std::move(test);
            }
            lua_pop(lua, 1);
        }
        lua_pop(lua, 1);
    }

    /**
     * Call the given function after executing the given Lua script.
     *
     * @param[in] file
     *     This is the contents of the Lua script file to execute.
     *
     * @param[in] filePath
     *     This is the path to the Lua script file to execute.
     *
     * @param[in] fn
     *     This is the function to call after executing the Lua script.
     *
     * @return
     *     If any error occurs executing the Lua script, a human-readable
     *     description of the error is returned.  Otherwise, an empty
     *     string is returned.
     */
    std::string WithScript(
        const std::string& file,
        const std::string& filePath,
        std::function< void() > fn
    ) {
        const auto originalWorkingDirectory = SystemAbstractions::File::GetWorkingDirectory();
        SystemAbstractions::File::SetWorkingDirectory(ParentFolderPath(filePath));
        lua_settop(lua, 0);
        lua_pushcfunction(lua, LuaTraceback);
        LuaReaderState luaReaderState;
        luaReaderState.chunk = &file;
        std::string errorMessage;
        switch (const int luaLoadResult = lua_load(lua, LuaReader, &luaReaderState, ("=" + filePath).c_str(), "t")) {
            case LUA_OK: {
                const int luaPCallResult = lua_pcall(lua, 0, 0, 1);
                if (luaPCallResult == LUA_OK) {
                    fn();
                } else {
                    if (!lua_isnil(lua, -1)) {
                        errorMessage = lua_tostring(lua, -1);
                    }
                }
            } break;
            case LUA_ERRSYNTAX: {
                errorMessage = lua_tostring(lua, -1);
            } break;
            case LUA_ERRMEM: {
                errorMessage = "LUA_ERRMEM";
            } break;
            case LUA_ERRGCMM: {
                errorMessage = "LUA_ERRGCMM";
            } break;
            default: {
                errorMessage = StringExtensions::sprintf("(unexpected lua_load result: %d)", luaLoadResult);
            } break;
        }
        lua_settop(lua, 0);
        SystemAbstractions::File::SetWorkingDirectory(originalWorkingDirectory);
        return errorMessage;
    }

    /**
     * Execute the Lua script in the given file, and gather information
     * about any test suites and tests registered by the script.
     *
     * @param[in,out] file
     *     This is the file from which to load the Lua script.
     *
     * @param[in] errorMessageDelegate
     *     This is the function to call to deliver any error messages
     *     generated while executing the Lua script.
     */
    void LoadTestSuite(
        SystemAbstractions::File& file,
        ErrorMessageDelegate errorMessageDelegate
    ) {
        if (!file.Open()) {
            errorMessageDelegate(
                StringExtensions::sprintf(
                    "ERROR: Unable to open Lua script file '%s'",
                    file.GetPath().c_str()
                )
            );
            return;
        }
        SystemAbstractions::IFile::Buffer buffer(file.GetSize());
        const auto amountRead = file.Read(buffer);
        file.Close();
        if (amountRead != buffer.size()) {
            errorMessageDelegate(
                StringExtensions::sprintf(
                    "ERROR: Unable to read Lua script file '%s'",
                    file.GetPath().c_str()
                )
            );
            return;
        }
        const std::string script(
            buffer.begin(),
            buffer.end()
        );
        WithLua([&]{
            auto errorMessage = WithScript(
                script,
                file.GetPath(),
                std::bind(&Impl::FindTests, this, script, file.GetPath())
            );
            if (!errorMessage.empty()) {
                errorMessageDelegate(
                    StringExtensions::sprintf(
                        "ERROR: Unable to load Lua script file '%s': %s",
                        file.GetPath().c_str(),
                        errorMessage.c_str()
                    )
                );
                return;
            }
            const auto delimiterIndex = file.GetPath().find_last_of('/');
            auto name = (
                (delimiterIndex == std::string::npos)
                ? file.GetPath()
                : file.GetPath().substr(delimiterIndex + 1)
            );
            if (name.length() >= luaFileExtensionLength) {
                const auto nameLengthWithoutExtension = name.length() - luaFileExtensionLength;
                if (name.substr(nameLengthWithoutExtension) == luaFileExtension) {
                    name = name.substr(0, nameLengthWithoutExtension);
                }
            }
        });
    }

    /**
     * Compare the two values at the top of the Lua stack and throw an error if
     * they are not equal.
     *
     * This is registered as the "assert_eq" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertEq(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (
            lua_istable(lua, 2)
            && lua_istable(lua, 3)
        ) {
            std::vector< Json::Value > keyChain;
            const auto comparisonResult = CompareLuaTables(lua, 2, 3, keyChain);
            if (!comparisonResult.empty()) {
                std::vector< std::string > keyChainAsStrings;
                for (const auto& key: keyChain) {
                    keyChainAsStrings.push_back(key.ToEncoding());
                }
                luaL_error(
                    lua,
                    "Tables differ (path: %s) -- %s\n",
                    StringExtensions::Join(keyChainAsStrings, ".").c_str(),
                    comparisonResult.c_str()
                );
            }
        } else if (!lua_compare(lua, 2, 3, LUA_OPEQ)) {
            luaL_error(
                lua,
                "Expected '%s', actual was '%s'\n",
                lua_tostring(lua, 2),
                lua_tostring(lua, 3)
            );
        }
        return 0;
    }

    /**
     * Compare the value at the top of the Lua stack and throw an error if
     * it is not considered "falsey".
     *
     * This is registered as the "assert_false" method of the "moonunit"
     * singleton provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertFalse(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (lua_toboolean(lua, 2)) {
            const std::string actual = luaL_tolstring(lua, 2, NULL);
            lua_pop(lua, 1);
            luaL_error(
                lua,
                "Expected '%s' to be false\n",
                actual.c_str()
            );
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and throw an error if
     * the first is not greater than or equal to the second.
     *
     * This is registered as the "assert_ge" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertGe(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (lua_compare(lua, 2, 3, LUA_OPLT)) {
            luaL_error(
                lua,
                "expected '%s' >= '%s'\n",
                lua_tostring(lua, 2),
                lua_tostring(lua, 3)
            );
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and throw an error if
     * the first is not greater than the second.
     *
     * This is registered as the "assert_gt" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertGt(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (lua_compare(lua, 2, 3, LUA_OPLE)) {
            luaL_error(
                lua,
                "expected '%s' > '%s'\n",
                lua_tostring(lua, 2),
                lua_tostring(lua, 3)
            );
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and throw an error if
     * the first is not less than or equal to the second.
     *
     * This is registered as the "assert_le" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertLe(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (!lua_compare(lua, 2, 3, LUA_OPLE)) {
            luaL_error(
                lua,
                "expected '%s' <= '%s'\n",
                lua_tostring(lua, 2),
                lua_tostring(lua, 3)
            );
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and throw an error if
     * the first is not less than the second.
     *
     * This is registered as the "assert_lt" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertLt(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (!lua_compare(lua, 2, 3, LUA_OPLT)) {
            luaL_error(
                lua,
                "expected '%s' < '%s'\n",
                lua_tostring(lua, 2),
                lua_tostring(lua, 3)
            );
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and throw an error if
     * they are equal.
     *
     * This is registered as the "assert_ne" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertNe(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (
            lua_istable(lua, 2)
            && lua_istable(lua, 3)
        ) {
            std::vector< Json::Value > keyChain;
            const auto comparisonResult = CompareLuaTables(lua, 2, 3, keyChain);
            if (comparisonResult.empty()) {
                luaL_error(
                    lua,
                    "Tables should differ but are the same\n"
                );
            }
        } else if (lua_compare(lua, 2, 3, LUA_OPEQ)) {
            luaL_error(
                lua,
                "Expected not '%s', actual was '%s'\n",
                lua_tostring(lua, 2),
                lua_tostring(lua, 3)
            );
        }
        return 0;
    }

    /**
     * Compare the value at the top of the Lua stack and throw an error if
     * it is not considered "truthy".
     *
     * This is registered as the "assert_true" method of the "moonunit"
     * singleton provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaAssertTrue(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (!lua_toboolean(lua, 2)) {
            const std::string actual = luaL_tolstring(lua, 2, NULL);
            lua_pop(lua, 1);
            luaL_error(
                lua,
                "Expected '%s' to be true\n",
                actual.c_str()
            );
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and mark the current
     * test as failed if the two values are not equal.
     *
     * This is registered as the "expect_eq" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectEq(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        bool expectationFailed = false;
        if (
            lua_istable(lua, 2)
            && lua_istable(lua, 3)
        ) {
            std::vector< Json::Value > keyChain;
            const auto comparisonResult = CompareLuaTables(lua, 2, 3, keyChain);
            if (!comparisonResult.empty()) {
                expectationFailed = true;
                std::vector< std::string > keyChainAsStrings;
                for (const auto& key: keyChain) {
                    keyChainAsStrings.push_back(key.ToEncoding());
                }
                self->errorMessageDelegate(
                    StringExtensions::sprintf(
                        "Tables differ (path: %s) -- %s\n",
                        StringExtensions::Join(keyChainAsStrings, ".").c_str(),
                        comparisonResult.c_str()
                    )
                );
            }
        } else if (!lua_compare(lua, 2, 3, LUA_OPEQ)) {
            expectationFailed = true;
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "Expected '%s' (%s), actual was '%s' (%s)\n",
                    lua_tostring(lua, 2),
                    luaL_typename(lua, 2),
                    lua_tostring(lua, 3),
                    luaL_typename(lua, 3)
                )
            );
        }
        if (expectationFailed) {
            self->currentTestFailed = true;
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the value at the top of the Lua stack and mark the current test
     * as failed if it is not considered "falsey".
     *
     * This is registered as the "expect_false" method of the "moonunit"
     * singleton provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectFalse(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (lua_toboolean(lua, 2)) {
            self->currentTestFailed = true;
            const std::string actual = luaL_tolstring(lua, 2, NULL);
            lua_pop(lua, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "Expected '%s' to be false\n",
                    actual.c_str()
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and mark the current
     * test as failed if the first is not greater than or equal to the second.
     *
     * This is registered as the "expect_ge" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectGe(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (lua_compare(lua, 2, 3, LUA_OPLT)) {
            self->currentTestFailed = true;
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "expected '%s' >= '%s'\n",
                    lua_tostring(lua, 2),
                    lua_tostring(lua, 3)
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and mark the current
     * test as failed if the first is not greater than the second.
     *
     * This is registered as the "expect_gt" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectGt(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (lua_compare(lua, 2, 3, LUA_OPLE)) {
            self->currentTestFailed = true;
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "expected '%s' > '%s'\n",
                    lua_tostring(lua, 2),
                    lua_tostring(lua, 3)
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and mark the current
     * test as failed if the first is not less than or equal to the second.
     *
     * This is registered as the "expect_le" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectLe(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (!lua_compare(lua, 2, 3, LUA_OPLE)) {
            self->currentTestFailed = true;
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "expected '%s' <= '%s'\n",
                    lua_tostring(lua, 2),
                    lua_tostring(lua, 3)
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and mark the current
     * test as failed if the first is not less than the second.
     *
     * This is registered as the "expect_lt" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectLt(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (!lua_compare(lua, 2, 3, LUA_OPLT)) {
            self->currentTestFailed = true;
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "expected '%s' < '%s'\n",
                    lua_tostring(lua, 2),
                    lua_tostring(lua, 3)
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the two values at the top of the Lua stack and mark the current
     * test as failed if they are equal.
     *
     * This is registered as the "expect_ne" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectNe(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (
            lua_istable(lua, 2)
            && lua_istable(lua, 3)
        ) {
            std::vector< Json::Value > keyChain;
            const auto comparisonResult = CompareLuaTables(lua, 2, 3, keyChain);
            if (comparisonResult.empty()) {
                self->currentTestFailed = true;
                self->errorMessageDelegate(
                    StringExtensions::sprintf(
                        "Tables should differ but are the same\n"
                    )
                );
            }
        } else if (lua_compare(lua, 2, 3, LUA_OPEQ)) {
            self->currentTestFailed = true;
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "Expected not '%s', actual was '%s'\n",
                    lua_tostring(lua, 2),
                    lua_tostring(lua, 3)
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Compare the value at the top of the Lua stack and mark the current test
     * as failed if it is not considered "truthy".
     *
     * This is registered as the "expect_true" method of the "moonunit"
     * singleton provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaExpectTrue(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        if (!lua_toboolean(lua, 2)) {
            self->currentTestFailed = true;
            const std::string actual = luaL_tolstring(lua, 2, NULL);
            lua_pop(lua, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "Expected '%s' to be true\n",
                    actual.c_str()
                )
            );
            luaL_traceback(lua, lua, NULL, 1);
            self->errorMessageDelegate(
                StringExtensions::sprintf(
                    "%s\n",
                    lua_tostring(lua, -1)
                )
            );
            lua_pop(lua, 1);
        }
        return 0;
    }

    /**
     * Register the given function as the test with the given name under the
     * test suite with the given name.
     *
     * This is registered as the "test" method of the "moonunit" singleton
     * provided to Lua scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     */
    static int LuaTest(lua_State* lua) {
        auto self = *(Impl**)luaL_checkudata(lua, 1, "moonunit");
        const std::string testSuiteName = luaL_checkstring(lua, 2);
        const std::string testName = luaL_checkstring(lua, 3);
        luaL_checktype(lua, 4, LUA_TFUNCTION);
        lua_rawgeti(lua, LUA_REGISTRYINDEX, self->luaRegistryIndex);
        lua_pushvalue(lua, 2);
        lua_rawget(lua, -2);
        if (lua_isnil(lua, -1)) {
            lua_pop(lua, 1);
            lua_newtable(lua);
            lua_pushvalue(lua, 2);
            lua_pushvalue(lua, -2);
            lua_rawset(lua, -4);
        }
        lua_pushvalue(lua, 3);
        lua_pushvalue(lua, 4);
        lua_rawset(lua, -3);
        lua_pop(lua, 2);
        return 0;
    }

    /**
     * Call the given function within the context of a fresh Lua interpreter
     * equipped with a "moonunit" singleton used to interact with the test
     * runner.
     *
     * @param[in] fn
     *     This is the function to call within the context of a fresh Lua
     *     interpreter equipped with a "moonunit" singleton used to interact
     *     with the test runner.
     */
    void WithLua(std::function< void() > fn) {
        // Create the Lua interpreter.
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
        luaL_newmetatable(lua, "moonunit");
        lua_pushstring(lua, "__index");
        lua_pushvalue(lua, -2);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_eq");
        lua_pushcfunction(lua, Impl::LuaAssertEq);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_false");
        lua_pushcfunction(lua, Impl::LuaAssertFalse);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_ge");
        lua_pushcfunction(lua, Impl::LuaAssertGe);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_gt");
        lua_pushcfunction(lua, Impl::LuaAssertGt);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_le");
        lua_pushcfunction(lua, Impl::LuaAssertLe);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_lt");
        lua_pushcfunction(lua, Impl::LuaAssertLt);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_ne");
        lua_pushcfunction(lua, Impl::LuaAssertNe);
        lua_settable(lua, -3);
        lua_pushstring(lua, "assert_true");
        lua_pushcfunction(lua, Impl::LuaAssertTrue);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_eq");
        lua_pushcfunction(lua, Impl::LuaExpectEq);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_false");
        lua_pushcfunction(lua, Impl::LuaExpectFalse);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_ge");
        lua_pushcfunction(lua, Impl::LuaExpectGe);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_gt");
        lua_pushcfunction(lua, Impl::LuaExpectGt);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_le");
        lua_pushcfunction(lua, Impl::LuaExpectLe);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_lt");
        lua_pushcfunction(lua, Impl::LuaExpectLt);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_ne");
        lua_pushcfunction(lua, Impl::LuaExpectNe);
        lua_settable(lua, -3);
        lua_pushstring(lua, "expect_true");
        lua_pushcfunction(lua, Impl::LuaExpectTrue);
        lua_settable(lua, -3);
        lua_pushstring(lua, "test");
        lua_pushcfunction(lua, Impl::LuaTest);
        lua_settable(lua, -3);
        lua_pop(lua, 1);

        // Construct the "moonunit" singleton representing the runner.
        auto self = (Impl**)lua_newuserdata(lua, sizeof(Impl**));
        *self = this;
        luaL_setmetatable(lua, "moonunit");
        lua_setglobal(lua, "moonunit");

        // Make a table for organizing test and test suites.
        lua_newtable(lua);
        luaRegistryIndex = luaL_ref(lua, LUA_REGISTRYINDEX);

        // Perform the requested operation.
        fn();

        // Release table used for organizing test and test suites.
        luaL_unref(lua, LUA_REGISTRYINDEX, luaRegistryIndex);
        luaRegistryIndex = 0;

        // Destroy the Lua interpreter.
        lua_close(lua);
        lua = nullptr;
    }
};

Runner::~Runner() noexcept = default;
Runner::Runner(Runner&&) noexcept = default;
Runner& Runner::operator=(Runner&&) noexcept = default;

Runner::Runner()
    : impl_(new Impl())
{
}

void Runner::Configure(
    SystemAbstractions::File& configurationFile,
    ErrorMessageDelegate errorMessageDelegate
) {
    if (!configurationFile.Open()) {
        return;
    }
    SystemAbstractions::IFile::Buffer buffer(configurationFile.GetSize());
    const auto amountRead = configurationFile.Read(buffer);
    configurationFile.Close();
    if (amountRead != buffer.size()) {
        return;
    }
    const std::string configuration(buffer.begin(), buffer.end());
    for (const auto& line: StringExtensions::Split(configuration, '\n')) {
        auto searchPath = StringExtensions::Trim(line);
        if (!SystemAbstractions::File::IsAbsolutePath(searchPath)) {
            searchPath = (
                ParentFolderPath(configurationFile.GetPath())
                + "/"
                + searchPath
            );
        }
        SystemAbstractions::File possibleTestFile(searchPath);
        if (!possibleTestFile.IsExisting()) {
            continue;
        }
        if (possibleTestFile.IsDirectory()) {
            SystemAbstractions::File possibleOtherConfigurationFile(searchPath + "/.moonunit");
            if (possibleOtherConfigurationFile.IsExisting()) {
                Configure(possibleOtherConfigurationFile, errorMessageDelegate);
            } else {
                std::vector< std::string > filePaths;
                SystemAbstractions::File::ListDirectory(searchPath, filePaths);
                bool success = true;
                for (const auto& filePath: filePaths) {
                    if (
                        (filePath.length() >= luaFileExtensionLength)
                        && (filePath.substr(filePath.length() - luaFileExtensionLength) == luaFileExtension)
                    ) {
                        SystemAbstractions::File testSuiteFile(filePath);
                        impl_->LoadTestSuite(
                            testSuiteFile,
                            errorMessageDelegate
                        );
                    }
                }
            }
        } else {
            impl_->LoadTestSuite(
                possibleTestFile,
                errorMessageDelegate
            );
        }
    }
}

std::string Runner::GetReport() const {
    std::ostringstream buffer;
    size_t numTests = 0;
    for (const auto& testSuite: impl_->testSuites) {
        numTests += testSuite.second.tests.size();
    }
    buffer
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl
        << "<testsuites tests=\"" << numTests << "\" name=\"AllTests\">" << std::endl;
    for (const auto& testSuite: impl_->testSuites) {
        buffer
            << "  <testsuite name=\"" << testSuite.first
            << "\" tests=\"" << testSuite.second.tests.size()
            << "\">" << std::endl;
        for (const auto& test: testSuite.second.tests) {
            buffer
                << "    <testcase name=\"" << test.first << "\""
                << " file=\"" << test.second.filePath << "\""
                << " line=\"" << test.second.lineNumber << "\" />" << std::endl;
        }
        buffer << "  </testsuite>" << std::endl;
    }
    buffer << "</testsuites>" << std::endl;
    return buffer.str();
}

std::vector< std::string > Runner::GetTestNames(const std::string& testSuiteName) const {
    const auto testSuitesEntry = impl_->testSuites.find(testSuiteName);
    if (testSuitesEntry == impl_->testSuites.end()) {
        return {};
    }
    const auto& testSuite = testSuitesEntry->second;
    std::vector< std::string > testNames;
    for (const auto& test: testSuite.tests) {
        testNames.push_back(test.first);
    }
    return testNames;
}

std::vector< std::string > Runner::GetTestSuiteNames() const {
    std::vector< std::string > testSuiteNames;
    for (const auto& testSuite: impl_->testSuites) {
        testSuiteNames.push_back(testSuite.first);
    }
    return testSuiteNames;
}

bool Runner::RunTest(
    const std::string& testSuiteName,
    const std::string& testName,
    ErrorMessageDelegate errorMessageDelegate
) {
    const auto testSuitesEntry = impl_->testSuites.find(testSuiteName);
    if (testSuitesEntry == impl_->testSuites.end()) {
        errorMessageDelegate(
            StringExtensions::sprintf(
                "ERROR: No test suite '%s' found",
                testSuiteName.c_str()
            )
        );
        return false;
    }
    const auto& testSuite = testSuitesEntry->second;
    const auto testsEntry = testSuite.tests.find(testName);
    if (testsEntry == testSuite.tests.end()) {
        errorMessageDelegate(
            StringExtensions::sprintf(
                "ERROR: No test '%s' found in test suite '%s'",
                testName.c_str(),
                testSuiteName.c_str()
            )
        );
        return false;
    }
    const auto& test = testsEntry->second;
    impl_->currentTestFailed = false;
    impl_->WithLua([&]{
        auto errorMessage = impl_->WithScript(
            test.file,
            test.filePath,
            [&]{
                impl_->errorMessageDelegate = errorMessageDelegate;
                lua_pushcfunction(impl_->lua, LuaTraceback);
                lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, impl_->luaRegistryIndex);
                lua_pushstring(impl_->lua, testSuiteName.c_str());
                lua_rawget(impl_->lua, -2);
                lua_remove(impl_->lua, -2);
                lua_pushstring(impl_->lua, testName.c_str());
                lua_rawget(impl_->lua, -2);
                lua_remove(impl_->lua, -2);
                const int luaPCallResult = lua_pcall(impl_->lua, 0, 0, 1);
                std::string errorMessage;
                if (luaPCallResult != LUA_OK) {
                    if (!lua_isnil(impl_->lua, -1)) {
                        errorMessageDelegate(
                            StringExtensions::sprintf(
                                "ERROR: %s\n",
                                lua_tostring(impl_->lua, -1)
                            )
                        );
                    }
                    lua_pop(impl_->lua, 1);
                    impl_->currentTestFailed = true;
                }
                impl_->errorMessageDelegate = nullptr;
            }
        );
        if (!errorMessage.empty()) {
            impl_->currentTestFailed = true;
            errorMessageDelegate(
                StringExtensions::sprintf(
                    "ERROR: Unable to load Lua script file '%s': %s",
                    test.filePath.c_str(),
                    errorMessage.c_str()
                )
            );
            return;
        }
    });
    return !impl_->currentTestFailed;
}
