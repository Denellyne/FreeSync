use std::{
    fs::{self, File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    sync::mpsc::{self, Receiver, Sender},
    thread,
};

#[derive(Debug)]
pub struct Logger {
    file: File,
    file_name: String,
    path: PathBuf,
    echo: bool,
    rx: Receiver<String>,
}

impl Logger {
    pub fn create(
        path: impl AsRef<Path>,
        file_name: String,
        append: bool,
        echo: bool,
    ) -> Sender<String> {
        let (mut logger, tx) =
            Logger::new(path, file_name, append, echo).expect("Unable to create logger");

        let _th = thread::spawn(move || logger.log_rcv());

        tx
    }

    fn new(
        path: impl AsRef<Path>,
        mut file_name: String,
        append: bool,
        echo: bool,
    ) -> Option<(Logger, Sender<String>)> {
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
                let (tx, rx): (Sender<String>, Receiver<String>) = mpsc::channel();
                Some((
                    Logger {
                        file,
                        file_name,
                        echo,
                        rx,
                        path: path.as_ref().to_path_buf(),
                    },
                    tx,
                ))
            }
            Err(e) => {
                eprintln!("{}", e);
                None
            }
        }
    }
    fn log_rcv(&mut self) {
        loop {
            let data = match self.rx.recv() {
                Ok(data) => data,
                Err(e) => e.to_string(),
            };
            self.write(data);
        }
    }

    fn write(&mut self, data: String) {
        let ts: i64 = time_format::now().unwrap_or_default();

        let date = time_format::strftime_utc("%a, %d %b %Y %T %Z", ts).unwrap_or_default();
        let data = format!("{date}: {data}\n");
        if let Err(e) = self.file.write_all(data.as_bytes()) {
            eprintln!("{e}");
            self.file = match OpenOptions::new()
                .create(true)
                .append(true)
                .open(&self.path)
            {
                Ok(file) => file,
                Err(e) => {
                    eprintln!("{}", e);
                    return;
                }
            };
            if let Err(e) = self.file.write_all(data.as_bytes()) {
                eprintln!("Couldn't write to file {}: {}", self.file_name, e);
                eprintln!("{data}");
                return;
            }
        }
        if self.echo {
            eprintln!("{data}");
        }
    }

    #[cfg(test)]
    pub fn log(&mut self, data: &str) -> Result<(), String> {
        match self.file.write_all(data.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(e.to_string()),
        }
    }
}

#[cfg(test)]
mod tests;
