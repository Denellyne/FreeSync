use std::fs;

use rand::random;
use tempfile::NamedTempFile;

use crate::mock::MockLogger;

fn random_data() -> String {
    let mut str: String = String::new();
    let len = random::<u8>() % u8::MAX / 4 + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    str
}

#[test]
fn test_logger() {
    let data = random_data();
    let mut log = MockLogger::new().unwrap();
    log.0.log(&data.clone()).unwrap();

    let read_data = fs::read(&log.0.file).expect("Unable to read file");
    assert_eq!(
        data.as_bytes(),
        read_data,
        "Written data and read data are different"
    );
}
