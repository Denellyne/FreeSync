use crate::merkle::diff::diff::Change;
use crate::merkle::merklenode::traits::{LeafData, LeafIO};
use crate::merkle::traits::{CompressedData, Hashable, IO};
use std::fs;
use std::fs::{File, OpenOptions};
use std::io::Write;
#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub struct LeafNode {
    pub hash: [u8; 32],
    pub compressed_data: Vec<u8>,
    pub file_path: PathBuf,
}

impl LeafNode {
    pub(crate) fn new(path: impl AsRef<Path>) -> Result<LeafNode, String> {
        let file_path = path.as_ref();
        match Self::hash_file(file_path) {
            Ok((hash, data_raw)) => {
                let mut data: Vec<u8> = format!("blob {}\0", data_raw.len()).into_bytes();
                data.extend_from_slice(&data_raw);
                Ok(LeafNode {
                    hash,
                    compressed_data: Self::compress(&data),
                    file_path: file_path.to_path_buf(),
                })
            }
            Err(e) => Err(e),
        }
    }
    pub(crate) fn from(path: impl AsRef<Path>, real_path: PathBuf) -> Result<LeafNode, String> {
        let file_path = path.as_ref();
        let raw_data = Self::read_file(file_path)?;
        let data_raw = Self::decompress_data(&raw_data)?;
        let hash = Self::hash(&data_raw);
        let mut data: Vec<u8> = format!("blob {}\0", data_raw.len()).into_bytes();
        data.extend_from_slice(&data_raw);

        Ok(LeafNode {
            hash,
            compressed_data: Self::compress(&data),
            file_path: real_path.to_path_buf(),
        })
    }
}

impl CompressedData for LeafNode {}
impl IO for LeafNode {}
impl Hashable for LeafNode {
    fn hash(vec: &[u8]) -> [u8; 32] {
        use sha2::{Digest, Sha256};
        Sha256::digest(vec).into()
    }
    fn get_hash(&self) -> [u8; 32] {
        self.hash
    }
}
impl LeafData for LeafNode {
    fn data(&self) -> &Vec<u8> {
        &self.compressed_data
    }

    fn diff_file(&self, other: &Self) -> Vec<Change> {
        fn diff<'a>(
            v1: &'a [u8],
            v2: &'a [u8],
            v1_start: u64,
            v2_start: u64,
        ) -> (Change, &'a [u8], &'a [u8], u64, u64) {
            fn should_delete(v1: &[u8], v2: &[u8]) -> bool {
                const LOOK: usize = 32;

                let d = v1.iter().take(LOOK).position(|&x| x == v2[0]);
                let i = v2.iter().take(LOOK).position(|&x| x == v1[0]);

                match (d, i) {
                    (Some(d), Some(i)) => d <= i,
                    (Some(_), None) => true,
                    (None, Some(_)) => false,
                    _ => true,
                }
            }

            if v1.is_empty() && !v2.is_empty() {
                return (
                    Change::Insert {
                        data: LeafNode::compress(v2),
                    },
                    &[],
                    &[],
                    v1_start,
                    v2_start + v2.len() as u64,
                );
            }
            if v2.is_empty() && !v1.is_empty() {
                return (
                    Change::Delete {
                        start: v1_start,
                        end: v1.len() as u64 - v1_start - 1,
                    },
                    &[],
                    &[],
                    v1_start + v1.len() as u64,
                    v2_start,
                );
            }
            if v1.is_empty() && v2.is_empty() {
                return (Change::End, &[], &[], v1_start, v2_start);
            }

            let mut len = 0;
            while len < v1.len() && len < v2.len() && v1[len] == v2[len] {
                len += 1;
            }
            if len > 0 {
                return (
                    Change::Copy {
                        start: v1_start,
                        end: v1_start + len as u64 - 1,
                    },
                    &v1[len..],
                    &v2[len..],
                    v1_start + len as u64,
                    v2_start + len as u64,
                );
            }

            if should_delete(v1, v2) {
                let delete_len = 1;
                (
                    Change::Delete {
                        start: v1_start,
                        end: v1_start + delete_len - 1,
                    },
                    &v1[delete_len as usize..],
                    v2,
                    v1_start + delete_len,
                    v2_start,
                )
            } else {
                let mut insert_len = 1;
                while insert_len < v2.len() && v1[0] != v2[insert_len] {
                    insert_len += 1;
                }
                (
                    Change::Insert {
                        data: LeafNode::compress(&v2[..insert_len]),
                    },
                    v1,
                    &v2[insert_len..],
                    v1_start,
                    v2_start + insert_len as u64,
                )
            }
        }

        let v1 = match LeafNode::decompress_data(&self.compressed_data) {
            Ok(leaf1_data) => leaf1_data,
            _ => panic!("Failed to decompress data of leaf1"),
        };
        let v2 = match LeafNode::decompress_data(&other.compressed_data) {
            Ok(leaf2_data) => leaf2_data,
            _ => panic!("Failed to decompress data of leaf2"),
        };
        let mut leaf1_data = v1.as_slice();
        let mut leaf2_data = v2.as_slice();

        let mut changes: Vec<Change> = Vec::new();

        let mut start_leaf1: u64 = 0;
        let mut start_leaf2: u64 = 0;
        loop {
            let change: Change;
            (change, leaf1_data, leaf2_data, start_leaf1, start_leaf2) =
                diff(leaf1_data, leaf2_data, start_leaf1, start_leaf2);
            match change {
                Change::End => {
                    changes.push(change);
                    return changes;
                }
                _ => changes.push(change),
            }
        }
    }

    fn decompress_data(data: &[u8]) -> Result<Vec<u8>, String> {
        fn to_num(vec: Vec<u8>) -> u64 {
            let mut num: u64 = 0;
            for n in vec {
                num = (num * 10) + (n - b'0') as u64;
            }
            num
        }

        let mut raw_data = LeafNode::decompress(data);
        raw_data.drain(0..5);

        let size: u64;
        (size, raw_data) = match Self::read_until_null(raw_data) {
            Ok((size_vec, data)) => (to_num(size_vec), data),
            Err(_) => return Err("Unable to retrieve size from data".to_string()),
        };
        if raw_data.len() as u64 != size {
            return Err(format!(
                "The size of the data is inconsistent, read size:{} buffer size:{}",
                size,
                raw_data.len()
            ));
        }

        Ok(raw_data)
    }
}

impl LeafIO for LeafNode {
    fn write_blob(&self, path: &Path) -> bool {
        let dir_path = path.join(&LeafNode::hash_to_hex_string(&self.hash)[..2]);
        if !dir_path.exists() {
            fs::create_dir_all(&dir_path).expect("Failed to create tree dir");
        }
        let file_path = dir_path.join(&LeafNode::hash_to_hex_string(&self.hash)[2..]);

        let mut file: File;
        file = OpenOptions::new()
            .create(true)
            .truncate(false)
            .write(true)
            .open(&file_path)
            .unwrap_or_else(|_| panic!("Unable to open file {}", file_path.display()));

        file.write_all(self.data())
            .expect("Failed to to write data");
        file.flush().expect("Failed to flush data");
        true
    }

    fn is_executable(&self) -> bool {
        #[cfg(unix)]
        {
            let metadata = fs::metadata(&self.file_path);
            let file_mode = metadata
                .unwrap_or_else(|_| {
                    panic!(
                        "Unable to read file metadata, file:{}",
                        self.file_path.display()
                    )
                })
                .permissions()
                .mode();
            if file_mode & 0o111 != 0 {
                return true;
            }
            false
        }

        #[cfg(windows)]
        {
            matches!(
                self.file_path.extension().and_then(|ext| ext.to_str()),
                Some("exe") | Some("bat") | Some("cmd") | Some("sh")
            )
        }
    }
}
