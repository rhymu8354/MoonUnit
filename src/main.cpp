/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2019 by Richard Walters
 */

#include "Runner.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <SystemAbstractions/File.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <unordered_set>
#include <vector>

namespace {

    /**
     * This function replaces all backslashes with forward slashes
     * in the given string.
     *
     * @param[in] in
     *     This is the string to fix.
     *
     * @return
     *     A copy of the given string, with all backslashes replaced
     *     with forward slashes, is returned.
     */
    std::string FixPathDelimiters(const std::string& in) {
        std::string out;
        for (auto c: in) {
            if (c == '\\') {
                out.push_back('/');
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    std::string CanonicalPath(std::string path) {
        path = FixPathDelimiters(path);
        if (!SystemAbstractions::File::IsAbsolutePath(path)) {
            path = (
                SystemAbstractions::File::GetWorkingDirectory()
                + "/"
                + path
            );
        }
        auto segmentsIn = SystemAbstractions::Split(path, '/');
        decltype(segmentsIn) segmentsOut;
        for (const auto& segment: segmentsIn) {
            if (segment == ".") {
                continue;
            }
            if (segment == "..") {
                if (segmentsOut.size() > 1) {
                    segmentsOut.pop_back();
                }
            } else {
                segmentsOut.push_back(segment);
            }
        }
        return SystemAbstractions::Join(segmentsOut, "/");
    }

    /**
     * Read and return the entire contents of the given file.
     *
     * @param[in,out] file
     *     This is the file to read.
     *
     * @return
     *     The contents of the file is returned.
     */
    std::string ReadFile(SystemAbstractions::File& file) {
        if (!file.Open()) {
            return "";
        }
        SystemAbstractions::IFile::Buffer buffer(file.GetSize());
        const auto amountRead = file.Read(buffer);
        file.Close();
        if (amountRead != buffer.size()) {
            return "";
        }
        return std::string(
            buffer.begin(),
            buffer.end()
        );
    }

    /**
     * This function prints to the standard error stream information
     * about how to use this program.
     */
    void PrintUsageInformation() {
        fprintf(
            stderr,
            (
                "Usage: MoonUnit [--path PATH]\n"
                "\n"
                "Do stuff with Lua and unit testing, maybe?\n"
                "\n"
                "Options:\n"
                "    PATH  The relative or absolute path to the folder containing the\n"
                "          Lua test scripts to run.\n"
            )
        );
    }

    /**
     * This contains variables set through the operating system environment
     * or the command-line arguments.
     */
    struct Environment {
        /**
         * This is the path to the folder which will be searched for Lua
         * scripts containing unit tests to run.
         */
        std::string searchPath = ".";

        std::string reportPath;
    };

    /**
     * This function updates the program environment to incorporate
     * any applicable command-line arguments.
     *
     * @param[in] argc
     *     This is the number of command-line arguments given to the program.
     *
     * @param[in] argv
     *     This is the array of command-line arguments given to the program.
     *
     * @param[in,out] environment
     *     This is the environment to update.
     *
     * @param[in] diagnosticMessageDelegate
     *     This is the function to call to publish any diagnostic messages.
     *
     * @return
     *     An indication of whether or not the function succeeded is returned.
     */
    bool ProcessCommandLineArguments(
        int argc,
        char* argv[],
        Environment& environment
    ) {
        FILE* log = fopen(
            (
                SystemAbstractions::File::GetExeParentDirectory()
                + "/log.txt"
            ).c_str(),
            "at"
        );
        fprintf(log, "MoonUnit executed; arguments:\n");
        enum class State {
            NoContext,
            SearchPath,
        } state = State::NoContext;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            fprintf(log, "  %s\n", arg.c_str());
            switch (state) {
                case State::NoContext: {
                    static const std::string reportArgumentPrefix = "--gtest_output=xml:";
                    static const auto reportArgumentPrefixLength = reportArgumentPrefix.length();
                    if (arg == "--path") {
                        state = State::SearchPath;
                    } else if (arg == "--help") {
                        printf(
                            "Running main() from ..\\googletest\\googletest\\src\\gtest_main.cc\n"
                            "This program contains tests written using Google Test. You can use the\n"
                            "following command line flags to control its behavior:\n"
                            "\n"
                            "Test Selection:\n"
                            "  --gtest_list_tests\n"
                            "      List the names of all tests instead of running them. The name of\n"
                            "      TEST(Foo, Bar) is \"Foo.Bar\".\n"
                            "  --gtest_filter=POSTIVE_PATTERNS[-NEGATIVE_PATTERNS]\n"
                            "      Run only the tests whose name matches one of the positive patterns but\n"
                            "      none of the negative patterns. '?' matches any single character; '*'\n"
                            "      matches any substring; ':' separates two patterns.\n"
                            "  --gtest_also_run_disabled_tests\n"
                            "      Run all disabled tests too.\n"
                            "\n"
                            "Test Execution:\n"
                            "  --gtest_repeat=[COUNT]\n"
                            "      Run the tests repeatedly; use a negative count to repeat forever.\n"
                            "  --gtest_shuffle\n"
                            "      Randomize tests' orders on every iteration.\n"
                            "  --gtest_random_seed=[NUMBER]\n"
                            "      Random number seed to use for shuffling test orders (between 1 and\n"
                            "      99999, or 0 to use a seed based on the current time).\n"
                            "\n"
                            "Test Output:\n"
                            "  --gtest_color=(yes|no|auto)\n"
                            "      Enable/disable colored output. The default is auto.\n"
                            "  --gtest_print_time=0\n"
                            "      Don't print the elapsed time of each test.\n"
                            "  --gtest_output=(json|xml)[:DIRECTORY_PATH\\|:FILE_PATH]\n"
                            "      Generate a JSON or XML report in the given directory or with the given\n"
                            "      file name. FILE_PATH defaults to test_detail.xml.\n"
                            "\n"
                            "Assertion Behavior:\n"
                            "  --gtest_break_on_failure\n"
                            "      Turn assertion failures into debugger break-points.\n"
                            "  --gtest_throw_on_failure\n"
                            "      Turn assertion failures into C++ exceptions for use by an external\n"
                            "      test framework.\n"
                            "  --gtest_catch_exceptions=0\n"
                            "      Do not report exceptions as test failures. Instead, allow them\n"
                            "      to crash the program or throw a pop-up (on Windows).\n"
                            "\n"
                            "Except for --gtest_list_tests, you can alternatively set the corresponding\n"
                            "environment variable of a flag (all letters in upper-case). For example, to\n"
                            "disable colored text output, you can either specify --gtest_color=no or set\n"
                            "the GTEST_COLOR environment variable to no.\n"
                            "\n"
                            "For more information, please read the Google Test documentation at\n"
                            "https://github.com/google/googletest/. If you find a bug in Google Test\n"
                            "(not one in your own code or tests), please report it to\n"
                            "<googletestframework@googlegroups.com>.\n"
                        );
                        exit(0);
                    } else if (arg.substr(0, reportArgumentPrefixLength) == reportArgumentPrefix) {
                        environment.reportPath = arg.substr(reportArgumentPrefixLength);
                    }
                } break;

                case State::SearchPath: {
                    environment.searchPath = arg;
                    state = State::NoContext;
                } break;

                default: break;
            }
        }
        (void)fclose(log);
        switch (state) {
            case State::SearchPath: {
                fprintf(
                    stderr,
                    "path expected for --path option\n"
                );
                return false;
            }

            default: break;
        }
        return true;
    }

}

/**
 * This function is the entrypoint of the program.
 *
 * @param[in] argc
 *     This is the number of command-line arguments given to the program.
 *
 * @param[in] argv
 *     This is the array of command-line arguments given to the program.
 */
int main(int argc, char* argv[]) {
    // Set up to catch memory leaks.
#ifdef _WIN32
    //_crtBreakAlloc = 18;
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif /* _WIN32 */

    // Process command line and environment variables.
    Environment environment;
    (void)setbuf(stdout, NULL);
    if (!ProcessCommandLineArguments(argc, argv, environment)) {
        PrintUsageInformation();
        return EXIT_FAILURE;
    }

    // Locate the highest-level ancestor folder of the current working
    // folder that contains a ".moonunit" file, and configure the runner
    // using it (and any other ".moonunit" files found indirectly).
    Runner runner;
    const auto searchPathSegments = SystemAbstractions::Split(
        CanonicalPath(environment.searchPath),
        '/'
    );
    for (size_t i = 1; i <= searchPathSegments.size(); ++i) {
        std::vector< std::string > possibleConfigurationFilePathSegments(
            searchPathSegments.begin(),
            searchPathSegments.begin() + i
        );
        possibleConfigurationFilePathSegments.push_back(".moonunit");
        SystemAbstractions::File possibleConfigurationFile(
            SystemAbstractions::Join(possibleConfigurationFilePathSegments, "/")
        );
        if (possibleConfigurationFile.IsExisting()) {
            runner.Configure(
                possibleConfigurationFile,
                [](const std::string& message){
                    (void)fwrite(message.data(), message.length(), 1, stderr);
                }
            );
        }
    }

    // Run all unit tests.
    bool success = true;
    for (const auto& testSuiteName: runner.GetTestSuiteNames()) {
        for (const auto& testName: runner.GetTestNames(testSuiteName)) {
            printf(
                "%s.%s...",
                testSuiteName.c_str(),
                testName.c_str()
            );
            std::vector< std::string > errorMessages;
            if (
                runner.RunTest(
                    testSuiteName,
                    testName,
                    [&](const std::string& message){
                        errorMessages.push_back(message);
                    }
                )
            ) {
                printf("** PASS **\n");
            } else {
                printf("** FAIL **\n");
                success = false;
            }
            if (!errorMessages.empty()) {
                fprintf(stderr, "------------------------\n");
                for (const auto& line: errorMessages) {
                    (void)fwrite(
                        line.data(),
                        line.length(), 1,
                        stderr
                    );
                }
                fprintf(stderr, "------------------------\n");
            }
        }
    }

    // Done.
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
