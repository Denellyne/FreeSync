use crate::merkle::node::{Change, Node};
use flate2::read::ZlibDecoder;
use std::fs;
use std::fs::{File, OpenOptions, ReadDir};
use std::io::Write;
use std::path::Path;

pub trait Hashable {
    fn hash(vec: &[u8]) -> [u8; 32];
    fn hash_to_hex_string(hash: &[u8; 32]) -> String {
        hash.iter().map(|b| format!("{:02x}", b)).collect()
    }

    fn get_hash(&self) -> [u8; 32];
}
pub trait HashableNode: Hashable + TreeIO {
    fn hash_tree(vec: &[Node]) -> [u8; 32];
}
pub trait CompressedData {
    fn compress(data: &[u8]) -> Vec<u8> {
        use flate2::write::ZlibEncoder;
        use flate2::Compression;
        use std::io::prelude::*;

        let mut encoder = ZlibEncoder::new(Vec::new(), Compression::best());
        encoder.write_all(data).expect("Unable to write data");

        encoder.finish().expect("Unable to finish compression")
    }
    fn decompress(data: &[u8]) -> Vec<u8> {
        use std::io::prelude::*;

        let mut decoder = ZlibDecoder::new(data);
        let mut decompressed: Vec<u8> = Vec::new();
        decoder
            .read_to_end(&mut decompressed)
            .expect("Error decompressing data");

        decompressed
    }
}
pub(in crate::merkle) trait LeafData: CompressedData {
    fn data(&self) -> &Vec<u8>;
    fn diff_file(&self, other: &Self) -> Vec<Change>;
}

pub(in crate::merkle) trait EntryData {
    const REGULAR_FILE: &'static [u8; 6] = b"100644";
    const EXECUTABLE_FILE: &'static [u8; 6] = b"100755";
    const SYMBOLIC_LINK: &'static [u8; 6] = b"120000";
    const DIRECTORY: &'static [u8; 6] = b"040000";
}

pub(in crate::merkle) mod internal_traits {
    use std::fs::{File, OpenOptions};
    use std::io::Write;
    use std::path::Path;
    pub trait TreeIOInternal {
        const MAIN_FOLDER: &'static str = ".\\.freesync";
        const OBJ_FOLDER: &'static str = ".\\.freesync\\objects";
        const HEAD_FILE: &'static str = ".\\.freesync\\HEAD";
        fn init(&self) -> bool;

        fn write_tree(&self) -> bool;

        fn write_file(&self, path: impl AsRef<Path>, data: impl AsRef<[u8]>) -> bool {
            let mut file: File;
            file = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&path)
                .expect("Unable to open file");

            if let Err(e) = file.write_all(data.as_ref()) {
                eprintln!("Unable to write file, {}", e);
                return false;
            }
            if let Err(e) = file.flush() {
                eprintln!("Unable to flush file, {}", e);
                return false;
            }
            true
        }
    }
}

pub trait TreeIO: internal_traits::TreeIOInternal {
    fn save_tree(&self) -> bool;
}

pub(in crate::merkle) trait LeafIO: LeafData {
    fn write_blob(&self, path: &Path) -> bool;
    fn is_executable(&self) -> bool;
}

pub trait IO {
    fn read_file(path: impl AsRef<Path>) -> Result<Vec<u8>, String> {
        match fs::read(&path) {
            Ok(data) => Ok(data),
            Err(e) => Err(e.to_string()),
        }
    }

    fn read_dir(path: impl AsRef<Path>) -> Result<ReadDir, String> {
        let paths = fs::read_dir(&path);
        match paths {
            Ok(paths) => Ok(paths),
            Err(e) => Err(e.to_string()),
        }
    }
    fn write_file(path: impl AsRef<Path>, data: &[u8]) {
        let mut file: File;
        file = OpenOptions::new()
            .create(true)
            .truncate(false)
            .write(true)
            .open(&path)
            .expect("Unable to open file");

        file.write_all(data).expect("Unable to write data");
        file.flush().expect("Unable to flush data");
    }
}
