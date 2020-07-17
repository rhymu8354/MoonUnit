use std::io::Read;

struct Test {
    file: String,
    file_path: String,
    line_number: usize,
    // test_fn: mlua::Function,
}

#[derive(Default)]
struct TestSuite {
    tests: std::collections::HashMap<String, Test>,
}

type TestSuites = std::collections::HashMap<String, TestSuite>;

trait FixPathNonsense {
    fn fix_silly_path_delimiter_nonsense(&self) -> std::borrow::Cow<str>;
}

impl FixPathNonsense for &str {
    #[cfg(target_os = "windows")]
    fn fix_silly_path_delimiter_nonsense(
        &self
    ) -> std::borrow::Cow<str> {
        self.replace("/", "\\").into()
    }

    #[cfg(not(target_os = "windows"))]
    fn fix_silly_path_delimiter_nonsense(
        &self
    ) -> std::borrow::Cow<str> {
        String::from(self)
    }
}

#[derive(Clone)]
struct TestSuitesHolder {
    inner: std::rc::Rc<std::cell::RefCell<TestSuites>>
}

impl TestSuitesHolder {
    fn new() -> Self {
        Self {
            inner: std::rc::Rc::new(
                std::cell::RefCell::new(
                    TestSuites::new()
                )
            ),
        }
    }
}

impl mlua::UserData for TestSuitesHolder {
    fn add_methods<'lua, M: mlua::UserDataMethods<'lua, Self>>(methods: &mut M) {
        methods.add_method_mut(
            "test",
            |
                _lua,
                this,
                (suite, name, test): (String, String, mlua::Function)
            | {
                let mut test_suites = this.inner.borrow_mut();
                let suite = test_suites.entry(suite).or_default();
                let test_source = dbg!(test.source());
                #[allow(clippy::cast_sign_loss)]
                suite.tests.entry(name).or_insert_with(
                    || Test{
                        file: String::from("TBD"),
                        file_path: String::from("TBD"),
                        line_number: test_source.line_defined as usize,
                        // test_fn: test,
                    }
                );
                // println!("Test: {}:{}", suite, name);
                Ok(())
            }
        );
    }
}

pub struct Runner {
    test_suites: TestSuitesHolder,
}

impl Runner {
    pub fn configure<E, P>(
        &mut self,
        configuration_file_path: P,
        mut error_delegate: &mut E
    ) where
        E: FnMut(&str),
        P: AsRef<std::path::Path>
    {
        let configuration_file_path = configuration_file_path.as_ref();
        let mut configuration_file = match std::fs::File::open(configuration_file_path) {
            Ok(file) => file,
            Err(_) => return,
        };
        let mut configuration = String::new();
        if configuration_file.read_to_string(&mut configuration).is_err() {
            return;
        }
        for line in configuration.lines() {
            let mut search_path = std::path::PathBuf::from(
                line.trim().fix_silly_path_delimiter_nonsense().as_ref()
            );
            if !search_path.is_absolute() {
                search_path = configuration_file_path
                    .parent()
                    .unwrap()
                    .join(search_path);
            }
            if !search_path.exists() {
                println!("{} does not exist.", search_path.display());
                println!(
                    "{} {} a directory",
                    search_path.display(),
                    if search_path.is_dir() { "is" } else { "is not" }
                );
                continue;
            }
            if search_path.is_dir() {
                let possible_other_configuration_file = search_path.join(".moonunit");
                if possible_other_configuration_file.is_file() {
                    self.configure(possible_other_configuration_file, error_delegate);
                } else {
                    for path in std::fs::read_dir(&search_path)
                        .unwrap()
                        .map(|dir_entry| dir_entry.unwrap().path())
                        .filter(|path| {
                            path.extension()
                                .map_or(false, |extension| extension == "lua")
                        })
                    {
                        self.load_test_suite(path, &mut error_delegate);
                    }
                }
            } else {
                self.load_test_suite(search_path, &mut error_delegate);
            }
        }
    }

    fn find_tests(
        &mut self,
        lua: &mut mlua::Lua,
        script: &str,
        path: &std::path::Path,
    ) {
    }

    pub fn get_test_suites(
        &self,
    ) -> impl std::iter::Iterator<Item=String> {
        // We have to completely enumerate the test suites and form
        // a new vector/iterator since the test suites are inside a cell,
        // meaning you can't get to them without borrowing, which we're
        // not allowed to return (can't return something we borrow inside).
        self.test_suites
            .inner
            .borrow()
            .keys()       // Iterate the test suite keys (the names of them)
            .cloned()     // Make copies of each name
            .collect::<Vec<String>>()  // Push them all into a vector
            .into_iter()  // Turn this into an iterator
    }

    pub fn load_test_suite<E, P>(
        &mut self,
        file_path: P,
        error_delegate: &mut E,
    ) where
        E: FnMut(&str),
        P: AsRef<std::path::Path>
    {
        let file_path = file_path.as_ref();
        let mut file = if let Ok(file) = std::fs::File::open(file_path) {
            file
        } else {
            error_delegate(
                &format!(
                    "ERROR: Unable to open Lua script file '{}'",
                    file_path.display()
                )
            );
            return;
        };
        let mut script = String::new();
        if file.read_to_string(&mut script).is_err() {
            error_delegate(
                &format!(
                    "ERROR: Unable to read Lua script file '{}'",
                    file_path.display()
                )
            );
            return;
        }
        self.with_lua(|runner, lua| {
            match runner.with_script(
                lua,
                &script,
                file_path,
                |runner, lua| runner.find_tests(lua, &script, file_path)
            ) {
                Ok(_) => (),
                Err(error) => {
                    error_delegate(
                        &format!(
                            "ERROR: Unable to load Lua script file '{}': {}",
                            file_path.display(),
                            error
                        )
                    );
                },
            }
        });
    }

    pub fn new() -> Self {
        Self {
            test_suites: TestSuitesHolder::new(),
        }
    }

    fn with_lua<F>(
        &mut self,
        f: F
    ) where
        F: FnOnce(
            &mut Self,
            &mut mlua::Lua
        )
    {
        let mut lua = mlua::Lua::new();
        lua.globals().set(
            "moonunit",
            self.test_suites.clone()
        ).unwrap();
        f(self, &mut lua);
    }

    fn with_script<F>(
        &mut self,
        lua: &mut mlua::Lua,
        script: &str,
        path: &std::path::Path,
        f: F,
    ) -> Result<(), String> where
        F: FnOnce(
            &mut Self,
            &mut mlua::Lua
        )
    {
        let original_working_directory = std::env::current_dir().unwrap();
        std::env::set_current_dir(path.parent().unwrap()).unwrap();
        let name: String = "=".to_string() + &path.to_string_lossy().to_string();
        let result = (move || {
            lua
                .load(script)
                .set_name(name.as_bytes())
                .and_then(mlua::Chunk::exec)
                .map_err(|err| err.to_string())?;
            f(self, lua);
            Ok(())
        })();
        std::env::set_current_dir(original_working_directory).unwrap();
        result
    }

}
