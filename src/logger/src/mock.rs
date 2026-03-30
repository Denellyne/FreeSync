use std::io::Write;
use std::{
    sync::mpsc::{self, Receiver, Sender},
    thread,
};

use crate::traits::Log;
use tempfile::NamedTempFile;

#[derive(Debug)]
pub struct MockLogger {
    pub file: NamedTempFile,
    rx: Receiver<String>,
}

impl Log for MockLogger {
    fn write(&mut self, data: String) {
        println!("{data}");
    }

    fn log_rcv(&mut self) {
        loop {
            let data = match self.rx.recv() {
                Ok(data) => data,
                Err(_) => continue,
            };
            self.write(data);
        }
    }
}
impl MockLogger {
    pub fn create() -> Sender<String> {
        let (mut logger, tx) = MockLogger::new().expect("Unable to create logger");

        let _th = thread::spawn(move || logger.log_rcv());

        tx
    }

    pub(crate) fn new() -> Option<(MockLogger, Sender<String>)> {
        use tempfile::NamedTempFile;

        let file = NamedTempFile::new().unwrap();
        let (tx, rx): (Sender<String>, Receiver<String>) = mpsc::channel();
        Some((MockLogger { file, rx }, tx))
    }
    pub fn log(&mut self, data: &str) -> Result<(), String> {
        match self.file.write_all(data.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(e.to_string()),
        }
    }
}
