/**
 * @file Runner.hpp
 *
 * This module declares the Runner class.
 *
 * © 2019 by Richard Walters
 */

#include <functional>
#include <memory>
#include <string>
#include <SystemAbstractions/File.hpp>
#include <vector>

/**
 * This class encapsulates all the details concerned with executing Lua
 * unit tests.
 */
class Runner {
    // Types
public:
    using ErrorMessageDelegate = std::function< void(const std::string& message) >;

    // Lifecycle Methods
public:
    ~Runner() noexcept;
    Runner(const Runner&) = delete;
    Runner(Runner&&) noexcept;
    Runner& operator=(const Runner&) = delete;
    Runner& operator=(Runner&&) noexcept;

    // Public Methods
public:
    /**
     * This is the constructor of the class.
     */
    Runner();

    void Configure(
        SystemAbstractions::File& configurationFile,
        ErrorMessageDelegate errorMessageDelegate
    );

    /**
     * Return a report, conforming to the report output of Google Test,
     * that provides details about the tests found and/or run.
     *
     * @return
     *     A report, conforming to the report output of Google Test,
     *     that provides details about the tests found and/or run is returned.
     */
    std::string GetReport() const;

    /**
     * Return the names of all tests in the given Lua test suite.
     *
     * @param[in] testSuiteName
     *     This is the name of the test suite for which to return the list
     *     of tests.
     *
     * @return
     *     The collection of names of tests found in the Lua test suite
     *     with the given name is returned.
     */
    std::vector< std::string > GetTestNames(const std::string& testSuiteName) const;

    /**
     * Return the names of all Lua test suites found.
     *
     * @return
     *     The collection of names of Lua test suites found is returned.
     */
    std::vector< std::string > GetTestSuiteNames() const;

    /**
     * Execute the Lua test with the given name in the given suite.
     *
     * Any problems with the test will be reported to the given
     * error message delegate.
     *
     * @param[in] testSuiteName
     *     This is the name of the test suite containing
     *     the Lua test to execute.
     *
     * @param[in] testName
     *     This is the name of the Lua test to execute.
     *
     * @param[in] errorMessageDelegate
     *     This is the function to call to report any error messages.
     *
     * @return
     *     An indication of whether or not the test passed is returned.
     */
    bool RunTest(
        const std::string& testSuiteName,
        const std::string& testName,
        ErrorMessageDelegate errorMessageDelegate
    );

    // Private properties
private:
    /**
     * This is the type of structure that contains the private
     * properties of the instance.  It is defined in the implementation
     * and declared here to ensure that it is scoped inside the class.
     */
    struct Impl;

    /**
     * This contains the private properties of the instance.
     */
    std::unique_ptr< Impl > impl_;
};
