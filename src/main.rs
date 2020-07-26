#![warn(clippy::pedantic)]

mod runner;

use structopt::StructOpt;
use std::io::Write;

#[allow(clippy::doc_markdown)]
#[structopt(verbatim_doc_comment)]
/// NOTE: The block below is required to fool 'C++ TestMate' -- DO NOT TOUCH
/// ----------------------------------------------------------------
/// This program contains tests written using Google Test.
///   --gtest_list_tests
///       List the names of all tests instead of running them
/// ----------------------------------------------------------------
///
/// Well, not really, but we had to say that in order for
/// the 'C++ TestMate' plugin for VSCode to *think* so, in order
/// for it to support this test runner.
///
/// What this program *actually* contains is a Lua interpreter and code which
/// discovers and executes unit tests written in Lua.  Place a '.moonunit' file
/// in the root folder of your project, and in that file list paths from there
/// to individual Lua test files to run, or paths to directories containing
/// other '.moonunit' files and/or Lua test files, and MoonUnit will discover
/// all your tests and run them for you, provided you either set the working
/// directory somewhere inside your project, or specify the project's folder using
/// the --path command-line argument.  Neat, huh?
///
/// What's really cool is MoonUnit makes its output look like Google Test,
/// and supports the minimum command-line arguments required by
/// the 'C++ TestMate' plugin for VSCode,
/// so that it should seamlessly integrate into a VSCode 'solution' along with
/// other test runners.
#[derive(Clone, Debug, StructOpt)]
struct Opts {
    /// The relative or absolute path to a folder which contains
    /// (or has a direct ancestor folder which contains) a '.moonunit' file
    /// specifying paths to directories containing Lua test files to run
    /// (or other '.moonunit' files) or individual Lua test files to run.
    /// If not specified, the current working directory is used instead.
    #[structopt(long, default_value=".")]
    path: std::path::PathBuf,

    /// List the names of all tests instead of running them
    #[structopt(long = "gtest_list_tests")]
    gtest_list_tests: bool,

    /// One or more test names separated by colons, which selects
    /// just the named tests to be run.
    /// If not specified, all discovered tests will be run.
    #[structopt(long = "gtest_filter")]
    gtest_filter: Option<String>,

    /// The relative or absolute path to an XML file to be generated
    /// containing a report about the tests discovered by the test runner,
    /// in a format compatible with Google Test.
    /// Unless this is specified, no report will be generated.
    #[structopt(long = "gtest_output")]
    gtest_output: Option<String>,

    #[structopt(long = "gtest_color")]
    gtest_color: Option<String>,

    #[structopt(long = "gtest_also_run_disabled_tests")]
    gtest_also_run_disabled_tests: bool,
}

#[allow(clippy::too_many_lines)]
fn app() -> i32 {
    // Parse all command-line options.
    let opts: Opts = Opts::from_args();

    // Locate the highest-level ancestor folder of the current working
    // folder that contains a ".moonunit" file, and configure the runner
    // using it (and any other ".moonunit" files found indirectly).
    let mut runner = runner::Runner::new();
    for path in opts.path.canonicalize().unwrap()
        .ancestors()
        .collect::<Vec<_>>()
        .into_iter()
        .rev()
    {
        let mut possible_configuration_file = path.to_path_buf();
        possible_configuration_file.push(".moonunit");
        if possible_configuration_file.is_file() {
            runner.configure(
                &possible_configuration_file,
                |message| {
                    eprintln!("{}", message);
                }
            )
        }
    }

    // List or run all unit tests.
    let mut success = true;
    let mut selected_tests = std::collections::HashMap::new();
    let mut total_tests = 0;
    let mut total_test_suites = 0;
    match opts.gtest_filter {
        None => {
            for test_suite_name in runner.get_test_suite_names() {
                total_test_suites += 1;
                total_tests += runner.get_test_names(test_suite_name).count();
            }
        },
        Some(filter) => {
            println!("Note: Google Test filter = {}", filter);
            for filter in filter.split(':') {
                total_test_suites += 1;
                if let Some(delimiter_index) = filter.find('.') {
                    let test_suite_name = &filter[0..delimiter_index];
                    let test_name = &filter[delimiter_index+1..];
                    if selected_tests
                        .entry(test_suite_name.to_owned())
                        .or_insert_with(std::collections::HashSet::new)
                        .insert(test_name.to_owned())
                    {
                        total_tests += 1;
                    }
                }
            }
        },
    };
    if !opts.gtest_list_tests {
        println!(
            "[==========] Running {} test{} from {} test suite{}.",
            total_tests,
            if total_tests == 1 { "" } else { "s" },
            total_test_suites,
            if total_test_suites == 1 { "" } else { "s" }
        );
        println!("[----------] Global test environment set-up.");
    }
    let mut passed = 0;
    let mut failed = Vec::new();
    let runner_start_time = std::time::Instant::now();
    for test_suite_name in runner.get_test_suite_names() {
        let selected_tests_entry = selected_tests.get(&test_suite_name);
        if !selected_tests.is_empty() && selected_tests_entry.is_none() {
            continue;
        }
        if opts.gtest_list_tests {
            println!("{}.", test_suite_name);
        } else if let Some(selected_tests_entry) = selected_tests_entry {
            println!(
                "[----------] {} test{} from {}",
                selected_tests_entry.len(),
                if selected_tests_entry.len() == 1 { "" } else { "s" },
                test_suite_name
            );
        }
        let test_suite_start_time = std::time::Instant::now();
        for test_name in runner.get_test_names(&test_suite_name) {
            if let Some(selected_tests_entry) = selected_tests_entry {
                if selected_tests_entry.get(&test_name).is_none() {
                    continue;
                }
            }
            if opts.gtest_list_tests {
                println!("  {}", test_name);
            } else {
                println!(
                    "[ RUN      ] {}.{}",
                    test_suite_name,
                    test_name,
                );
                let error_messages = std::cell::RefCell::new(Vec::new());
                let test_start_time = std::time::Instant::now();
                let test_passed = runner.run_test(
                    &test_suite_name,
                    &test_name,
                    |message| error_messages.borrow_mut().push(message)
                );
                let error_messages = error_messages.borrow();
                let test_elapsed_time = test_start_time.elapsed().as_millis();
                if test_passed {
                    passed += 1;
                    println!(
                        "[       OK ] {}.{} ({} ms)",
                        test_suite_name,
                        test_name,
                        test_elapsed_time,
                    );
                } else {
                    failed.push(
                        format!("{}.{}", test_suite_name, test_name)
                    );
                    if !error_messages.is_empty() {
                        for line in error_messages.iter() {
                            println!("{}", line);
                        }
                    }
                    println!(
                        "[  FAILED  ] {}.{} ({} ms)",
                        test_suite_name,
                        test_name,
                        test_elapsed_time,
                    );
                    success = false;
                }
            }
        }
        let test_suite_elapsed_time = test_suite_start_time.elapsed().as_millis();
        if !opts.gtest_list_tests {
            if let Some(selected_tests_entry) = selected_tests_entry {
                println!(
                    "[----------] {} test{} from {} ({} ms total)\n",
                    selected_tests_entry.len(),
                    if selected_tests_entry.len() == 1 { "" } else { "s" },
                    test_suite_name,
                    test_suite_elapsed_time,
                );
            }
        }
    }
    let runner_elapsed_time = runner_start_time.elapsed().as_millis();
    if !opts.gtest_list_tests {
        println!("[----------] Global test environment tear-down");
        println!(
            "[==========] {} test{} from {} test suite{} ran. ({} ms total)",
            total_tests,
            if total_tests == 1 { "" } else { "s" },
            total_test_suites,
            if total_test_suites == 1 { "" } else { "s" },
            runner_elapsed_time,
        );
        println!(
            "[  PASSED  ] {} test{}.",
            passed,
            if passed == 1 { "" } else { "s" },
        );
    }
    if !failed.is_empty() {
        println!(
            "[  FAILED  ] {} test{}, listed below:",
            failed.len(),
            if failed.len() == 1 { "" } else { "s" },
        );
        for instance in &failed {
            println!(
                "[  FAILED  ] {}",
                instance
            );
        }
        println!();
        println!(
            " {} FAILED TEST{}",
            failed.len(),
            if failed.len() == 1 { "" } else { "S" },
        );
    }

    // Generate report if requested.
    if let Some(gtest_output) = opts.gtest_output {
        if gtest_output.starts_with("xml:") {
            let report_path = &gtest_output[4..];
            if let Ok(mut report_file) = std::fs::File::create(report_path) {
                report_file.write_all(runner.get_report().as_bytes()).unwrap();
            }
        }
    }

    // Done.
    if success { 0 } else { 1 }
}

fn main() {
    std::process::exit(app())
}
