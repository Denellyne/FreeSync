use crate::Logger;
use std::fs;

use rand::random;
use tempfile::NamedTempFile;

fn random_data() -> String {
    let mut str: String = String::new();
    let len = random::<u16>() % u16::MAX / 4 + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    str
}

#[test]
fn test_logger() {
    let temp_file = NamedTempFile::new().expect("Unable to create temp file");
    let data = random_data();
    let mut log = Logger::new(&temp_file, false).expect("Unable to open logger on temporary file");
    log.log(data.clone()).unwrap();

    let read_data = fs::read(&temp_file).expect("Unable to read file");
    assert_eq!(
        data.as_bytes(),
        read_data,
        "Written data and read data are different"
    );
}
