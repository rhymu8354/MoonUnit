/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2019 by Richard Walters
 */

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <SystemAbstractions/File.hpp>

namespace {

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
                "Usage: MoonUnit\n"
                "\n"
                "Do stuff with Lua and unit testing, maybe?\n"
            )
        );
    }

    /**
     * This contains variables set through the operating system environment
     * or the command-line arguments.
     */
    struct Environment {
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
        enum class State {
            Initial,
        } state = State::Initial;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            switch (state) {
                case State::Initial: {
                } break;

                default: break;
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
    (void)setbuf(stdout, NULL);
    if (!ProcessCommandLineArguments(argc, argv, environment)) {
        PrintUsageInformation();
        return EXIT_FAILURE;
    }

    // Do something observable! :LUL:
    printf("Hello, World!\n");

    // Done.
    return EXIT_SUCCESS;
}
