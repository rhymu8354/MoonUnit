pub struct Runner {
}

impl Runner {
    pub fn configure<E, P>(
        &mut self,
        configuration_file: P,
        _e: E
    ) where
        E: FnMut(&str),
        P: AsRef<std::path::Path>
    {
        println!(
            "Configuration file: {}",
            configuration_file.as_ref().to_string_lossy()
        );
    }

    pub fn new() -> Self {
        Self {
        }
    }
}
