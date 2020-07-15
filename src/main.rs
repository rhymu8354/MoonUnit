mod runner;

use structopt::StructOpt;

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
}

fn main() {
    // Parse all command-line options.
    let opts: Opts = Opts::from_args();

    // Locate the highest-level ancestor folder of the current working
    // folder that contains a ".moonunit" file, and configure the runner
    // using it (and any other ".moonunit" files found indirectly).
    let mut runner = runner::Runner::new();
    let canonical_path = opts.path.canonicalize().unwrap();
    let search_path_segments: Vec<_> = canonical_path.components().collect();
    for i in 2..search_path_segments.len()+1 {
        let mut possible_configuration_file: std::path::PathBuf =
            search_path_segments[0..i].iter().collect();
        possible_configuration_file.push(".moonunit");
        println!(
            "Possible configuration file: {}",
            possible_configuration_file.to_string_lossy()
        );
        if possible_configuration_file.exists() {
            runner.configure(
                &possible_configuration_file,
                |message| {
                    eprintln!("{}", message);
                }
            )
        }
    }

    // for (size_t i = 1; i <= searchPathSegments.size(); ++i) {
    //     std::vector< std::string > possibleConfigurationFilePathSegments(
    //         searchPathSegments.begin(),
    //         searchPathSegments.begin() + i
    //     );
    //     possibleConfigurationFilePathSegments.push_back(".moonunit");
    //     SystemAbstractions::File possibleConfigurationFile(
    //         StringExtensions::Join(possibleConfigurationFilePathSegments, "/")
    //     );
    //     if (possibleConfigurationFile.IsExisting()) {
    //         runner.Configure(
    //             possibleConfigurationFile,
    //             [](const std::string& message){
    //                 (void)fwrite(message.data(), message.length(), 1, stderr);
    //             }
    //         );
    //     }
    // }
}
