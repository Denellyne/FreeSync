use std::{
    fs::{File, OpenOptions},
    io::Write,
    path::Path,
};

#[derive(Debug)]
pub struct Logger {
    file: File,
}

impl Logger {
    pub fn new(path: impl AsRef<Path>, append: bool) -> Option<Logger> {
        match OpenOptions::new()
            .create(true)
            .append(append)
            .write(true)
            .open(&path)
        {
            Ok(file) => Some(Logger { file }),
            Err(e) => {
                eprintln!("{}", e);
                None
            }
        }
    }

    #[cfg(test)]
    pub fn log(&mut self, data: String) -> Result<(), String> {
        match self.file.write_all(data.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(e.to_string()),
        }
    }
    #[cfg(not(test))]
    pub fn log(&mut self, data: String) -> Result<(), String> {
        let ts: i64 = match time_format::now() {
            Ok(ts) => ts,
            Err(e) => return Err(e.to_string()),
        };

        let date = time_format::strftime_utc("%a, %d %b %Y %T %Z", ts).unwrap();
        let data = format!("{date}: {data}");
        match self.file.write_all(data.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(e.to_string()),
        }
    }
}
#[cfg(test)]
mod tests;
