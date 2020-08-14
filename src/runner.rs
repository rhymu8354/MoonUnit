use std::io::Read;
use std::fmt::Write;

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
        std::borrow::Cow::from(*self)
    }
}

struct Test {
    file: String,
    path: std::path::PathBuf,
    line_number: usize,
}

#[derive(Default)]
struct TestSuite {
    tests: std::collections::HashMap<String, Test>,
}

type TestSuites = std::collections::HashMap<String, TestSuite>;

struct RunnerInner {
    current_test_failed: bool,
    test_suites: TestSuites,
}

impl RunnerInner {
    fn new() -> Self {
        Self {
            current_test_failed: false,
            test_suites: TestSuites::new(),
        }
    }
}

fn render(value: &mlua::Value) -> String {
    match value {
        mlua::Value::Nil => {
            String::from("nil")
        },
        mlua::Value::Boolean(value) => {
            format!("{}", value)
        },
        mlua::Value::Integer(value) => {
            format!("{}", value)
        },
        mlua::Value::Number(value) => {
            format!("{}", value)
        },
        mlua::Value::String(value) => {
            format!("\"{}\"", value.to_str().unwrap())
        },
        _ => {
            format!("{:?}", value)
        },
    }
}

struct LuaValueForDisplay<'lua>(&'lua mlua::Value<'lua>);

impl<'lua> std::fmt::Display for LuaValueForDisplay<'lua> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self.0 {
            mlua::Value::Nil => {
                write!(f, "nil")
            },
            mlua::Value::Boolean(value) => {
                write!(f, "{} (boolean)", value)
            },
            mlua::Value::Integer(value) => {
                write!(f, "{} (integer)", value)
            },
            mlua::Value::Number(value) => {
                write!(f, "{} (number)", value)
            },
            mlua::Value::String(value) => {
                write!(f, "\"{}\" (string)", value.to_str().unwrap())
            },
            _ => {
                write!(f, "{:?}", self.0)
            },
        }
    }
}

struct OrderedLuaValue<'lua>(mlua::Value<'lua>);

impl<'lua> PartialEq for OrderedLuaValue<'lua> {
    fn eq(&self, other: &Self) -> bool {
        self.0.eq(&other.0)
    }
}

impl<'lua> Eq for OrderedLuaValue<'lua> {
    fn assert_receiver_is_total_eq(&self) {}
}

impl<'lua> PartialOrd for OrderedLuaValue<'lua> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl<'lua> Ord for OrderedLuaValue<'lua> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        let self_type_name = self.0.type_name();
        let other_type_name = other.0.type_name();
        if self_type_name == other_type_name {
            match &self.0 {
                mlua::Value::Boolean(value) => {
                    if let mlua::Value::Boolean(other_value) = &other.0 {
                        value.cmp(other_value)
                    } else {
                        panic!()
                    }
                },
                mlua::Value::Integer(value) => {
                    if let mlua::Value::Integer(other_value) = &other.0 {
                        value.cmp(other_value)
                    } else {
                        panic!()
                    }
                },
                mlua::Value::Number(value) => {
                    if let mlua::Value::Number(other_value) = &other.0 {
                        if value < other_value {
                            std::cmp::Ordering::Less
                        } else if value > other_value {
                            std::cmp::Ordering::Greater
                        } else {
                            std::cmp::Ordering::Equal
                        }
                    } else {
                        panic!()
                    }
                },
                mlua::Value::String(value) => {
                    if let mlua::Value::String(other_value) = &other.0 {
                        value.to_str().unwrap().cmp(other_value.to_str().unwrap())
                    } else {
                        panic!()
                    }
                },
                _ => {
                    std::cmp::Ordering::Equal
                },
            }
        } else {
            self_type_name.cmp(other_type_name)
        }
    }
}

struct RunContext {
    errors: std::rc::Rc<std::cell::RefCell<Vec<String>>>,
    file: String,
    path: std::path::PathBuf,
    runner: Runner,
    tests_registry_key: std::rc::Rc<mlua::RegistryKey>,
}

impl mlua::UserData for RunContext {
    fn add_methods<'lua, M: mlua::UserDataMethods<'lua, Self>>(methods: &mut M) {
        methods.add_method("test", moonunit_test);
        methods.add_method("assert_eq", moonunit_assert_eq);
        methods.add_method("assert_ne", moonunit_assert_ne);
        methods.add_method("assert_ge", moonunit_assert_ge);
        methods.add_method("assert_gt", moonunit_assert_gt);
        methods.add_method("assert_le", moonunit_assert_le);
        methods.add_method("assert_lt", moonunit_assert_lt);
        methods.add_method("assert_true", moonunit_assert_true);
        methods.add_method("assert_false", moonunit_assert_false);
        methods.add_method("expect_eq", moonunit_expect_eq);
        methods.add_method("expect_ne", moonunit_expect_ne);
        methods.add_method("expect_ge", moonunit_expect_ge);
        methods.add_method("expect_gt", moonunit_expect_gt);
        methods.add_method("expect_le", moonunit_expect_le);
        methods.add_method("expect_lt", moonunit_expect_lt);
        methods.add_method("expect_true", moonunit_expect_true);
        methods.add_method("expect_false", moonunit_expect_false);
    }
}

fn moonunit_test<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (suite, name, test): (String, String, mlua::Function)
) -> mlua::Result<()> {
    // Get line number information about the provided function.
    let test_source = test.source();

    // Make sure there is a table for this suite of tests.
    let tests_table: mlua::Table = lua.registry_value(&this.tests_registry_key)?;
    if !tests_table.contains_key(suite.clone())? {
        tests_table.set(suite.clone(), lua.create_table()?)?;
    }

    // Store the function in the tests table.
    let tests: mlua::Table = tests_table.get(suite.clone())?;
    tests.set(name.clone(), test)?;

    // Add information about the test to the runner.
    let test_suites = &mut this.runner.inner.borrow_mut().test_suites;
    let suite = test_suites.entry(suite).or_default();
    #[allow(clippy::cast_sign_loss)]
    suite.tests.entry(name).or_insert_with(
        || Test{
            file: this.file.clone(),
            path: this.path.clone(),
            line_number: test_source.line_defined as usize,
        }
    );
    Ok(())
}

fn moonunit_assert_eq<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if let (mlua::Value::Table(lhs), mlua::Value::Table(rhs)) = (&lhs, &rhs) {
        let (message, key_chain) = RunContext::compare_lua_tables(lhs, rhs, Vec::new());
        if message.is_empty() {
            Ok(())
        } else {
            Err(mlua::Error::RuntimeError(
                format!(
                    "Tables differ (path: {}) -- {}",
                    key_chain
                        .into_iter()
                        .map(
                            |value| render(&value)
                        )
                        .fold(
                            String::new(),
                            |mut chain, key| {
                                if !chain.is_empty() {
                                    chain.push('.');
                                }
                                chain += &key;
                                chain
                            }
                        ),
                    message
                )
            ))
        }
    } else if lhs == rhs {
        Ok(())
    } else {
        Err(mlua::Error::RuntimeError(
            format!(
                "Expected {}, actual was {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        ))
    }
}

fn moonunit_assert_ne<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if let (mlua::Value::Table(lhs), mlua::Value::Table(rhs)) = (&lhs, &rhs) {
        let (message, _key_chain) = RunContext::compare_lua_tables(lhs, rhs, Vec::new());
        if message.is_empty() {
            Err(mlua::Error::RuntimeError(
                String::from("Tables should differ but are the same")
            ))
        } else {
            Ok(())
        }
    } else if lhs == rhs {
        Err(mlua::Error::RuntimeError(
            format!(
                "Expected not {}, actual was {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        ))
    } else {
        Ok(())
    }
}

fn moonunit_assert_ge<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) == std::cmp::Ordering::Less {
        Err(mlua::Error::RuntimeError(
            format!(
                "Expected {} >= {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        ))
    } else {
        Ok(())
    }
}

fn moonunit_assert_gt<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) == std::cmp::Ordering::Greater {
        Ok(())
    } else {
        Err(mlua::Error::RuntimeError(
            format!(
                "Expected {} > {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        ))
    }
}

fn moonunit_assert_le<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) == std::cmp::Ordering::Greater {
        Err(mlua::Error::RuntimeError(
            format!(
                "Expected {} <= {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        ))
    } else {
        Ok(())
    }
}

fn moonunit_assert_lt<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) == std::cmp::Ordering::Less {
        Ok(())
    } else {
        Err(mlua::Error::RuntimeError(
            format!(
                "Expected {} < {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        ))
    }
}

fn moonunit_assert_true<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (value,): (mlua::Value,)
) -> mlua::Result<()> {
    match &value {
        mlua::Value::Boolean(false) | mlua::Value::Nil => {
            Err(mlua::Error::RuntimeError(
                format!(
                    "Expected {} to be true",
                    LuaValueForDisplay(&value),
                )
            ))
        },
        _ => Ok(())
    }
}

fn moonunit_assert_false<'lua, 'runner>(
    _lua: &'lua mlua::Lua,
    _this: &'runner RunContext,
    (value,): (mlua::Value,)
) -> mlua::Result<()> {
    match &value {
        mlua::Value::Boolean(false) | mlua::Value::Nil => Ok(()),
        _ => {
            Err(mlua::Error::RuntimeError(
                format!(
                    "Expected {} to be false",
                    LuaValueForDisplay(&value),
                )
            ))
        },
    }
}

fn moonunit_expect_eq<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    let mut expectation_failed = false;
    if let (mlua::Value::Table(lhs), mlua::Value::Table(rhs)) = (&lhs, &rhs) {
        let (message, key_chain) = RunContext::compare_lua_tables(lhs, rhs, Vec::new());
        if !message.is_empty() {
            expectation_failed = true;
            this.errors.borrow_mut().push(
                format!(
                    "Tables differ (path: {}) -- {}",
                    key_chain
                        .into_iter()
                        .map(
                            |value| render(&value)
                        )
                        .fold(
                            String::new(),
                            |mut chain, key| {
                                if !chain.is_empty() {
                                    chain.push('.');
                                }
                                chain += &key;
                                chain
                            }
                        ),
                    message
                )
            )
        }
    } else if lhs != rhs {
        expectation_failed = true;
        this.errors.borrow_mut().push(
            format!(
                "Expected {}, actual was {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        );
    }
    if expectation_failed {
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_ne<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    let mut expectation_failed = false;
    if let (mlua::Value::Table(lhs), mlua::Value::Table(rhs)) = (&lhs, &rhs) {
        let (message, _key_chain) = RunContext::compare_lua_tables(lhs, rhs, Vec::new());
        if message.is_empty() {
            expectation_failed = true;
            this.errors.borrow_mut().push(
                String::from("Tables should differ but are the same")
            )
        }
    } else if lhs == rhs {
        expectation_failed = true;
        this.errors.borrow_mut().push(
            format!(
                "Expected not {}, actual was {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        );
    }
    if expectation_failed {
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_ge<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) == std::cmp::Ordering::Less {
        this.errors.borrow_mut().push(
            format!(
                "Expected {} >= {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        );
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_gt<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) != std::cmp::Ordering::Greater {
        this.errors.borrow_mut().push(
            format!(
                "Expected {} > {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        );
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_le<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) == std::cmp::Ordering::Greater {
        this.errors.borrow_mut().push(
            format!(
                "Expected {} <= {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        );
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_lt<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (lhs, rhs): (mlua::Value, mlua::Value)
) -> mlua::Result<()> {
    if OrderedLuaValue(lhs.clone()).cmp(&OrderedLuaValue(rhs.clone())) != std::cmp::Ordering::Less {
        this.errors.borrow_mut().push(
            format!(
                "Expected {} < {}",
                LuaValueForDisplay(&lhs),
                LuaValueForDisplay(&rhs),
            )
        );
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_true<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (value,): (mlua::Value,)
) -> mlua::Result<()> {
    let mut expectation_failed = false;
    match &value {
        mlua::Value::Boolean(false) | mlua::Value::Nil => {
            expectation_failed = true;
            this.errors.borrow_mut().push(
                format!(
                    "Expected {} to be true",
                    LuaValueForDisplay(&value),
                )
            );
        },
        _ => (),
    };
    if expectation_failed {
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

fn moonunit_expect_false<'lua, 'runner>(
    lua: &'lua mlua::Lua,
    this: &'runner RunContext,
    (value,): (mlua::Value,)
) -> mlua::Result<()> {
    let mut expectation_failed = false;
    match &value {
        mlua::Value::Boolean(false) | mlua::Value::Nil => (),
        _ => {
            expectation_failed = true;
            this.errors.borrow_mut().push(
                format!(
                    "Expected {} to be false",
                    LuaValueForDisplay(&value),
                )
            );
        },
    };
    if expectation_failed {
        this.runner.inner.borrow_mut().current_test_failed = true;
        let traceback: String = lua
            .load("debug.traceback(nil, 3)")
            .eval()?;
        this.errors.borrow_mut().push(traceback);
    }
    Ok(())
}

impl RunContext {
    fn compare_lua_tables<'lua>(
        lhs: &mlua::Table<'lua>,
        rhs: &mlua::Table<'lua>,
        mut key_chain: Vec<mlua::Value<'lua>>
    ) -> (String, Vec<mlua::Value<'lua>>) {
        let lhs_keys = lhs
            .clone()
            .pairs::<mlua::Value, mlua::Value>()
            .map(|pair| OrderedLuaValue(pair.unwrap().0));
        let mut rhs_keys = rhs
            .clone()
            .pairs::<mlua::Value, mlua::Value>()
            .map(|pair| OrderedLuaValue(pair.unwrap().0))
            .collect::<std::collections::BTreeSet<OrderedLuaValue>>();
        for key in lhs_keys {
            key_chain = match rhs_keys.get(&key) {
                None => {
                    return (
                        format!(
                            "Actual value missing key {}",
                            LuaValueForDisplay(&key.0)
                        ),
                        key_chain
                    );
                },
                Some(_) => {
                    let lhs = lhs.get(key.0.clone()).unwrap();
                    let rhs = rhs.get(key.0.clone()).unwrap();
                    let (message, key_chain) = if let (mlua::Value::Table(lhs), mlua::Value::Table(rhs)) = (&lhs, &rhs) {
                        key_chain.push(key.0.clone());
                        let (message, mut key_chain) = RunContext::compare_lua_tables(&lhs, &rhs, key_chain);
                        if message.is_empty() {
                            key_chain.pop();
                        }
                        (message, key_chain)
                    } else if lhs == rhs {
                        (String::from(""), key_chain)
                    } else {
                        key_chain.push(key.0.clone());
                        (
                            format!(
                                "Expected {}, actual was {}",
                                LuaValueForDisplay(&lhs),
                                LuaValueForDisplay(&rhs),
                            ),
                            key_chain
                        )
                    };
                    if !message.is_empty() {
                        return (message, key_chain);
                    }
                    rhs_keys.remove(&key);
                    key_chain
                },
            };
        }
        if rhs_keys.is_empty() {
            (String::from(""), key_chain)
        } else {
            (
                format!(
                    "Actual value has extra key {}",
                    LuaValueForDisplay(&rhs_keys.into_iter().next().unwrap().0)
                ),
                key_chain
            )
        }
    }

    fn new(
        errors: &std::rc::Rc<std::cell::RefCell<Vec<String>>>,
        file: &str,
        path: &std::path::Path,
        runner: &Runner,
        tests_registry_key: &std::rc::Rc<mlua::RegistryKey>,
    ) -> Self {
        Self {
            errors: errors.clone(),
            file: file.to_owned(),
            path: path.to_owned(),
            runner: runner.clone(),
            tests_registry_key: tests_registry_key.clone(),
        }
    }
}

#[derive(Clone)]
pub struct Runner {
    inner: std::rc::Rc<std::cell::RefCell<RunnerInner>>,
}

impl Runner {
    pub fn configure<E, P>(
        &mut self,
        configuration_file_path: P,
        error_delegate: E
    ) where
        E: FnMut(String) + Copy,
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
                        self.load_test_suite(path, error_delegate);
                    }
                }
            } else {
                self.load_test_suite(search_path, error_delegate);
            }
        }
    }

    pub fn get_report(&self) -> String {
        let mut num_tests = 0;
        for test_suite in self.inner.borrow().test_suites.values() {
            num_tests += test_suite.tests.len();
        }
        let mut buffer = String::new();
        writeln!(&mut buffer, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>").unwrap();
        writeln!(&mut buffer, "<testsuites tests=\"{}\" name=\"AllTests\">", num_tests).unwrap();
        for (test_suite_name, test_suite) in &self.inner.borrow().test_suites {
            writeln!(
                &mut buffer,
                "  <testsuite name=\"{}\" tests=\"{}\">",
                test_suite_name,
                test_suite.tests.len()
            ).unwrap();
            for (test_name, test) in &test_suite.tests {
                writeln!(
                    &mut buffer,
                    "    <testcase name=\"{}\" file=\"{}\" line=\"{}\" />",
                    test_name,
                    test.path.display(),
                    test.line_number,
                ).unwrap();
            }
            writeln!(&mut buffer, "</testsuite>").unwrap();
        }
        writeln!(&mut buffer, "</testsuites>").unwrap();
        buffer
    }

    pub fn get_test_names<S>(
        &self,
        suite: S,
    ) -> impl std::iter::Iterator<Item=String> where
        S: AsRef<str>
    {
        self.inner        // Start with our shared inner state
            .borrow()     // It's in a RefCell, so borrow its contents
            .test_suites  // From there visit our test suites hash map
            .get(suite.as_ref())  // look up a specific test suite
            .unwrap()     // It had better be in there!
            .tests        // From there visit its tests
            .keys()       // Iterate the test keys (the names of them)
            .cloned()     // Make copies of each name
            .collect::<Vec<_>>()  // Push them all into a vector
            .into_iter()  // Turn this into an iterator
    }

    pub fn get_test_suite_names(
        &self,
    ) -> impl std::iter::Iterator<Item=String> {
        // We have to completely enumerate the test suites and form
        // a new vector/iterator since the test suites are inside a cell,
        // meaning you can't get to them without borrowing, which we're
        // not allowed to return (can't return something we borrow inside).
        self.inner
            .borrow()
            .test_suites
            .keys()       // Iterate the test suite keys (the names of them)
            .cloned()     // Make copies of each name
            .collect::<Vec<_>>()  // Push them all into a vector
            .into_iter()  // Turn this into an iterator
    }

    pub fn load_test_suite<E, P>(
        &mut self,
        file_path: P,
        mut error_delegate: E,
    ) where
        E: FnMut(String) + Copy,
        P: AsRef<std::path::Path>
    {
        let file_path = file_path.as_ref();
        let mut file = if let Ok(file) = std::fs::File::open(file_path) {
            file
        } else {
            error_delegate(
                format!(
                    "ERROR: Unable to open Lua script file '{}'",
                    file_path.display()
                )
            );
            return;
        };
        let mut script = String::new();
        if file.read_to_string(&mut script).is_err() {
            error_delegate(
                format!(
                    "ERROR: Unable to read Lua script file '{}'",
                    file_path.display()
                )
            );
            return;
        }
        self.with_lua(|runner, lua| {
            match runner.with_script(
                lua,
                error_delegate,
                &script,
                file_path,
                |_, _, _| Ok(())
            ) {
                Ok(_) => (),
                Err(error) => {
                    error_delegate(
                        format!(
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
            inner: std::rc::Rc::new(std::cell::RefCell::new(RunnerInner::new())),
        }
    }

    fn lookup_test<S>(
        &self,
        suite: S,
        name: S,
    ) -> Result<(String, std::path::PathBuf), String> where
        S: AsRef<str>,
    {
        let runner = self.inner.borrow();
        let suite = suite.as_ref();
        let name = name.as_ref();
        let test_suite = if let Some(test_suite) = runner
            .test_suites
            .get(suite)
        {
            test_suite
        } else {
            return Err(
                format!(
                    "ERROR: No test suite '{}' found",
                    suite
                )
            );
        };
        let test = if let Some(test) = test_suite
            .tests
            .get(name)
        {
            test
        } else {
            return Err(
                format!(
                    "ERROR: No test '{}' found in test suite '{}'",
                    name,
                    suite,
                )
            );
        };
        let file = test.file.clone();
        let path = test.path.clone();
        Ok((file, path))
    }

    pub fn run_test<S, E>(
        &mut self,
        test_suite_name: S,
        test_name: S,
        mut error_delegate: E
    ) -> bool where
        S: AsRef<str>,
        E: FnMut(String) + Copy,
    {
        let (file, path) = match self.lookup_test(&test_suite_name, &test_name) {
            Ok((file, path)) => (file, path),
            Err(message) => {
                error_delegate(message);
                return false;
            }
        };
        self.inner.borrow_mut().current_test_failed = false;
        self.with_lua(|runner, lua| {
            match runner.with_script(
                lua,
                error_delegate,
                &file,
                &path,
                |runner, lua, tests_registry_key| {
                    let tests_table: mlua::Table = lua.registry_value(&tests_registry_key)?;
                    let tests: mlua::Table = tests_table.get(test_suite_name.as_ref())?;
                    let test: mlua::Function = tests.get(test_name.as_ref())?;
                    if let Err(error) = test.call::<_, ()>(()) {
                        if let mlua::Error::CallbackError{traceback, cause} = error {
                            error_delegate(
                                format!(
                                    "ERROR: {}",
                                    cause
                                )
                            );
                            error_delegate(traceback);
                        } else {
                            error_delegate(
                                format!(
                                    "ERROR: {}",
                                    error
                                )
                            );
                        }
                        runner.inner.borrow_mut().current_test_failed = true;
                    }
                    Ok(())
                },
            ) {
                Ok(_) => (),
                Err(message) => {
                    runner.inner.borrow_mut().current_test_failed = true;
                    error_delegate(
                        format!(
                            "ERROR: Unable to load Lua script file '{}': {}",
                            path.display(),
                            message
                        )
                    );
                },
            };
        });
        !self.inner.borrow().current_test_failed
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
        unsafe {
            let mut lua = mlua::Lua::unsafe_new();
            f(self, &mut lua)
        }
    }

    fn with_script<E, F>(
        &mut self,
        lua: &mut mlua::Lua,
        mut error_delegate: E,
        script: &str,
        path: &std::path::Path,
        f: F,
    ) -> Result<(), String> where
        E: FnMut(String),
        F: FnOnce(
            &mut Self,
            &mut mlua::Lua,
            std::rc::Rc<mlua::RegistryKey>,
        ) -> mlua::Result<()>,
    {
        let original_working_directory = std::env::current_dir().unwrap();
        std::env::set_current_dir(path.parent().unwrap()).unwrap();
        let name: String = "=".to_string() + &path.to_string_lossy().to_string();
        let result = (move || {
            let tests_table = lua.create_table().unwrap();
            let tests_registry_key = std::rc::Rc::new(lua.create_registry_value(tests_table).unwrap());
            let errors = std::rc::Rc::new(std::cell::RefCell::new(Vec::new()));
            lua
                .globals()
                .set(
                    "moonunit",
                    RunContext::new(&errors, script, path, self, &tests_registry_key)
                )
                .unwrap();
            lua
                .load(script)
                .set_name(name.as_bytes())
                .and_then(mlua::Chunk::exec)
                .map_err(|err| err.to_string())?;
            f(self, lua, tests_registry_key)
                .map_err(|err| err.to_string())?;
            for message in errors.borrow_mut().iter() {
                error_delegate(message.clone());
            }
            Ok(())
        })();
        std::env::set_current_dir(original_working_directory).unwrap();
        result
    }

}
