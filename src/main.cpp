/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2019 by Richard Walters
 */

#include "Runner.hpp"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <StringExtensions/StringExtensions.hpp>
#include <SystemAbstractions/File.hpp>
#include <SystemAbstractions/Time.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

    /**
     * Replace all backslashes with forward slashes
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

    /**
     * Return the absolute path, with breadcrumbs ("." or "..") removed, that
     * is equivalent to the given relative or absolute path that may contain
     * breadcrumbs.
     *
     * @param[in] path
     *     This is the path to make absolute and remove breadcrumbs.
     *
     * @return
     *     The absolute path, with breadcrumbs removed, that is equivalent
     *     to the given relative or absolute path that may contain breadcrumbs
     *     is returned.
     */
    std::string CanonicalPath(std::string path) {
        path = FixPathDelimiters(path);
        if (!SystemAbstractions::File::IsAbsolutePath(path)) {
            path = (
                SystemAbstractions::File::GetWorkingDirectory()
                + "/"
                + path
            );
        }
        auto segmentsIn = StringExtensions::Split(path, '/');
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
        return StringExtensions::Join(segmentsOut, "/");
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
        if (!file.OpenReadOnly()) {
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
        printf(
            "Usage: MoonUnit [--path=PATH]\n"
            "                [--gtest_list_tests]\n"
            "                [--gtest_filter=FILTER]\n"
            "                [--gtest_output=xml:REPORT]\n"
            "\n"
            "   or: MoonUnit --help\n"
            "\n"
            "Options:\n"
            "\n"
            "    PATH    The relative or absolute path to a folder which contains\n"
            "            (or has a direct ancestor folder which contains) a '.moonunit' file\n"
            "            specifying paths to directories containing Lua test files to run\n"
            "            (or other '.moonunit' files) or individual Lua test files to run.\n"
            "            If not specified, the current working directory is used instead.\n"
            "\n"
            "    FILTER  One or more test names separated by colons, which selects\n"
            "            just the named tests to be run.\n"
            "            If not specified, all discovered tests will be run.\n"
            "\n"
            "    REPORT  The relative or absolute path to an XML file to be generated\n"
            "            containing a report about the tests discovered by the test runner,\n"
            "            in a format compatible with Google Test.\n"
            "            Unless this is specified, no report will be generated.\n"
            "\n"
            "This program contains tests written using Google Test.\n"
            "\n"
            "Well, not really, but we had to say that in order for\n"
            "the 'Catch2 and Google Test Explorer' plugin for VSCode to *think* so, in order\n"
            "for it to support this test runner.\n"
            "\n"
            "What this program *actually* contains is a Lua interpreter and code which\n"
            "discovers and executes unit tests written in Lua.  Place a '.moonunit' file\n"
            "in the root folder of your project, and in that file list paths from there\n"
            "to individual Lua test files to run, or paths to directories containing\n"
            "other '.moonunit' files and/or Lua test files, and MoonUnit will discover\n"
            "all your tests and run them for you, provided you either set the working\n"
            "directory somewhere inside your project, or specify the project's folder using\n"
            "the --path command-line argument.  Neat, huh?\n"
            "\n"
            "What's really cool is MoonUnit makes its output look like Google Test,\n"
            "and supports the minimum command-line arguments required by\n"
            "the 'Catch2 and Google Test Explorer' plugin for VSCode,\n"
            "so that it should seamlessly integrate into a VSCode 'solution' along with\n"
            "other test runners.\n"
        );
    }

    /**
     * This contains variables set through the operating system environment
     * or the command-line arguments.
     */
    struct Environment {
        /**
         * This is the path to the folder used as a starting point
         * for locating MoonUnit configuration files (".moonunit") which
         * specify which directories and files to search for Lua tests.
         */
        std::string searchPath = ".";

        /**
         * If not empty, the program will generate an XML report
         * to the file at this path.
         */
        std::string reportPath;

        /**
         * If not empty, this holds a list (delimited by colons) of
         * the names of tests to run out of all the tests found.
         *
         * If this is empty, the program will run all tests found.
         */
        std::string filter;

        /**
         * This flag indicates whether or not the program will simply
         * list all tests found rather than running tests.
         */
        bool listTests = false;

        /**
         * This flag indicates whether or not the program will output
         * help/usage information and then exit without searching for
         * or running any tests.
         */
        bool helpRequested = false;
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
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            static const std::string pathOptionPrefix = "--path=";
            static const size_t pathOptionPrefixLength = pathOptionPrefix.length();
            static const std::string gtestFilterOptionPrefix = "--gtest_filter=";
            static const size_t gtestFilterOptionPrefixLength = gtestFilterOptionPrefix.length();
            static const std::string reportArgumentPrefix = "--gtest_output=xml:";
            static const auto reportArgumentPrefixLength = reportArgumentPrefix.length();
            if (arg.substr(0, pathOptionPrefixLength) == pathOptionPrefix) {
                environment.searchPath = arg.substr(pathOptionPrefixLength);
            } else if (arg == "--help") {
                environment.helpRequested = true;
            } else if (arg == "--gtest_list_tests") {
                environment.listTests = true;
            } else if (arg.substr(0, gtestFilterOptionPrefixLength) == gtestFilterOptionPrefix) {
                environment.filter = arg.substr(gtestFilterOptionPrefixLength);
            } else if (arg.substr(0, reportArgumentPrefixLength) == reportArgumentPrefix) {
                environment.reportPath = arg.substr(reportArgumentPrefixLength);
            }
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
    if (!ProcessCommandLineArguments(argc, argv, environment)) {
        PrintUsageInformation();
        return EXIT_FAILURE;
    }

    // If help is requested, print usage information and exit early.
    if (environment.helpRequested) {
        PrintUsageInformation();
        return EXIT_SUCCESS;
    }

    // Locate the highest-level ancestor folder of the current working
    // folder that contains a ".moonunit" file, and configure the runner
    // using it (and any other ".moonunit" files found indirectly).
    Runner runner;
    const auto searchPathSegments = StringExtensions::Split(
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
            StringExtensions::Join(possibleConfigurationFilePathSegments, "/")
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

    // List or run all unit tests.
    bool success = true;
    std::unordered_map< std::string, std::unordered_set< std::string > > selectedTests;
    size_t totalTests = 0, totalTestSuites = 0;
    if (environment.filter.empty()) {
        const auto testSuiteNames = runner.GetTestSuiteNames();
        totalTestSuites = testSuiteNames.size();
        for (const auto& testSuiteName: testSuiteNames) {
            totalTests += runner.GetTestNames(testSuiteName).size();
        }
    } else {
        printf("Note: Google Test filter = %s\n", environment.filter.c_str());
        for (const auto& filter: StringExtensions::Split(environment.filter, ':')) {
            ++totalTestSuites;
            const auto delimiterIndex = filter.find('.');
            if (delimiterIndex != std::string::npos) {
                const auto testSuiteName = filter.substr(0, delimiterIndex);
                const auto testName = filter.substr(delimiterIndex + 1);
                if (selectedTests[testSuiteName].insert(testName).second) {
                    ++totalTests;
                }
            }
        }
    }
    if (!environment.listTests) {
        printf(
            "[==========] Running %zu test%s from %zu test suite%s.\n"
            "[----------] Global test environment set-up.\n",
            totalTests,
            ((totalTests == 1) ? "" : "s"),
            totalTestSuites,
            ((totalTestSuites == 1) ? "" : "s")
        );
    }
    size_t passed = 0;
    std::vector< std::string > failed;
    SystemAbstractions::Time timer;
    const auto runnerStartTime = timer.GetTime();
    for (const auto& testSuiteName: runner.GetTestSuiteNames()) {
        const auto selectedTestsEntry = selectedTests.find(testSuiteName);
        if (
            !selectedTests.empty()
            && (selectedTestsEntry == selectedTests.end())
        ) {
            continue;
        }
        if (environment.listTests) {
            printf("%s.\n", testSuiteName.c_str());
        } else if (selectedTestsEntry != selectedTests.end()) {
            printf(
                "[----------] %zu test%s from %s\n",
                selectedTestsEntry->second.size(),
                ((selectedTestsEntry->second.size() == 1) ? "" : "s"),
                testSuiteName.c_str()
            );
        }
        const auto testSuiteStartTime = timer.GetTime();
        for (const auto& testName: runner.GetTestNames(testSuiteName)) {
            if (selectedTestsEntry != selectedTests.end()) {
                const auto selectedTestEntry = selectedTestsEntry->second.find(testName);
                if (selectedTestEntry == selectedTestsEntry->second.end()) {
                    continue;
                }
            }
            if (environment.listTests) {
                printf("  %s\n", testName.c_str());
            } else {
                printf(
                    "[ RUN      ] %s.%s\n",
                    testSuiteName.c_str(),
                    testName.c_str()
                );
                std::vector< std::string > errorMessages;
                const auto testStartTime = timer.GetTime();
                const auto testPassed = runner.RunTest(
                    testSuiteName,
                    testName,
                    [&](const std::string& message){
                        errorMessages.push_back(message);
                    }
                );
                const auto testEndTime = timer.GetTime();
                if (testPassed) {
                    ++passed;
                    printf(
                        "[       OK ] %s.%s (%d ms)\n",
                        testSuiteName.c_str(),
                        testName.c_str(),
                        (int)ceil((testEndTime - testStartTime) * 1000.0)
                    );
                } else {
                    failed.push_back(
                        StringExtensions::sprintf(
                            "%s.%s",
                            testSuiteName.c_str(),
                            testName.c_str()
                        )
                    );
                    if (!errorMessages.empty()) {
                        for (const auto& line: errorMessages) {
                            (void)fwrite(
                                line.data(),
                                line.length(), 1,
                                stdout
                            );
                        }
                    }
                    printf(
                        "[  FAILED  ] %s.%s (%d ms)\n",
                        testSuiteName.c_str(),
                        testName.c_str(),
                        (int)ceil((testEndTime - testStartTime) * 1000.0)
                    );
                    success = false;
                }
            }
        }
        const auto testSuiteEndTime = timer.GetTime();
        if (
            !environment.listTests
            && (selectedTestsEntry != selectedTests.end())
        ) {
            printf(
                "[----------] %zu test%s from %s (%d ms total)\n\n",
                selectedTestsEntry->second.size(),
                ((selectedTestsEntry->second.size() == 1) ? "" : "s"),
                testSuiteName.c_str(),
                (int)ceil((testSuiteEndTime - testSuiteStartTime) * 1000.0)
            );
        }
    }
    const auto runnerEndTime = timer.GetTime();
    if (!environment.listTests) {
        printf(
            "[----------] Global test environment tear-down\n"
            "[==========] %zu test%s from %zu test suite%s ran. (%d ms total)\n"
            "[  PASSED  ] %zu test%s.\n",
            totalTests,
            ((totalTests == 1) ? "" : "s"),
            totalTestSuites,
            ((totalTestSuites == 1) ? "" : "s"),
            (int)ceil((runnerEndTime - runnerStartTime) * 1000.0),
            passed,
            ((passed == 1) ? "" : "s")
        );
    }
    if (!failed.empty()) {
        printf(
            "[  FAILED  ] %zu test%s, listed below:\n",
            failed.size(),
            ((failed.size() == 1) ? "" : "s")
        );
        for (const auto& instance: failed) {
            printf(
                "[  FAILED  ] %s\n",
                instance.c_str()
            );
        }
        printf(
            "\n"
            " %zu FAILED TEST%s\n",
            failed.size(),
            ((failed.size() == 1) ? "" : "S")
        );
    }

    // Generate report if requested.
    if (!environment.reportPath.empty()) {
        FILE* reportFile = fopen(environment.reportPath.c_str(), "wt");
        if (reportFile != NULL) {
            const auto report = runner.GetReport();
            (void)fwrite(report.data(), report.length(), 1, reportFile);
            (void)fclose(reportFile);
        }
    }

    // Done.
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
