use std::{
    cell::RefCell,
    fs::{self, File, OpenOptions},
    io::Write,
    path::Path,
};

#[derive(Debug)]
pub struct Logger {
    file: RefCell<File>,
    file_name: String,
    echo: bool,
}

impl Logger {
    pub fn new(
        path: impl AsRef<Path>,
        mut file_name: String,
        append: bool,
        echo: bool,
    ) -> Option<Logger> {
        if file_name.is_empty() {
            file_name = path.as_ref().file_name()?.to_string_lossy().to_string();
        }
        let exists = append && fs::exists(path.as_ref()).unwrap_or(false);

        let parent = path.as_ref().parent()?;
        fs::create_dir_all(parent).ok()?;
        match OpenOptions::new()
            .create(true)
            .append(append)
            .write(true)
            .open(&path)
        {
            Ok(mut file) => {
                if !exists
                    && let Err(e) = file.write_all(format!("Logging {}\n", file_name).as_bytes())
                {
                    eprintln!("Couldn't write to file {}: {}", path.as_ref().display(), e);
                    return None;
                }
                let file = RefCell::from(file);
                Some(Logger {
                    file,
                    file_name,
                    echo,
                })
            }
            Err(e) => {
                eprintln!("{}", e);
                None
            }
        }
    }

    #[cfg(test)]
    pub fn log(&self, data: &str) -> Result<(), String> {
        match self.file.borrow_mut().write_all(data.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(e.to_string()),
        }
    }
    #[cfg(not(test))]
    pub fn log(&self, data: &str) {
        let ts: i64 = time_format::now().unwrap_or_default();

        let date = time_format::strftime_utc("%a, %d %b %Y %T %Z", ts).unwrap();
        let data = format!("{date}: {data}\n");
        if let Err(e) = self.file.borrow_mut().write_all(data.as_bytes()) {
            eprintln!("Couldn't write to file {}: {}", self.file_name, e);
            eprintln!("{data}");
            return;
        }
        if self.echo {
            eprintln!("{data}");
        }
    }
}

#[macro_export]
macro_rules! log_fmt {
    ($logger:expr, $($arg:tt)*) => {{
        $logger.log(&format!($($arg)*));
    }};
}

#[cfg(test)]
mod tests;
