use crate::merklenode::diff::Change;
use crate::merklenode::node::Node;
use crate::merklenode::traits::{LeafData, LeafIO};
use crate::traits::{CompressedData, Hashable, ReadFile};
use std::fs;
use std::io::Write;
#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use tempfile::NamedTempFile;

#[derive(Eq, PartialEq, Clone, Hash)]
pub struct LeafNode {
    pub hash: [u8; 32],
    pub compressed_data: Vec<u8>,
    pub file_path: PathBuf,
}

impl std::fmt::Debug for LeafNode {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.debug_struct("LeafNode")
            .field("File", &self.file_path)
            .finish()
    }
}

impl LeafNode {
    pub(crate) fn new(path: impl AsRef<Path>) -> Result<LeafNode, String> {
        assert!(
            path.as_ref().exists(),
            "There isn't any file in the path {}",
            path.as_ref().display()
        );
        let file_path = path.as_ref();
        match Self::hash_file(file_path) {
            Ok((hash, data_raw)) => {
                let mut data: Vec<u8> = format!("blob {}\0", data_raw.len()).into_bytes();
                data.extend_from_slice(&data_raw);

                Ok(LeafNode {
                    hash,
                    compressed_data: Self::compress(&data)?,
                    file_path: file_path.to_path_buf(),
                })
            }
            Err(e) => Err(e),
        }
    }
    pub(crate) fn from(path: impl AsRef<Path>, real_path: PathBuf) -> Result<LeafNode, String> {
        debug_assert!(
            path.as_ref().exists(),
            "There isn't any file in the path {}",
            path.as_ref().display()
        );

        let file_path = path.as_ref();
        let raw_data = Self::read_file(file_path)?;
        debug_assert!(
            !raw_data.is_empty(),
            "File is empty {}",
            file_path.display()
        );

        let data_raw = Self::decompress_data(&raw_data)?;
        let hash = Self::hash(&data_raw);
        let mut data: Vec<u8> = format!("blob {}\0", data_raw.len()).into_bytes();
        data.extend_from_slice(&data_raw);

        Ok(LeafNode {
            hash,
            compressed_data: Self::compress(&data)?,
            file_path: real_path.to_path_buf(),
        })
    }

    pub(crate) fn apply_blob(&mut self, changes: Vec<Change>) -> Result<(), String> {
        let mut uncompressed_data = Self::decompress_data(self.compressed_data.as_slice())?;
        let mut data_raw: Vec<u8> = Vec::with_capacity(uncompressed_data.len());

        debug_assert!(!changes.is_empty());

        for change in changes.into_iter() {
            match change {
                Change::Copy { start, end } => {
                    let slice = uncompressed_data
                        .drain(..=(end - start) as usize)
                        .collect::<Vec<u8>>();
                    data_raw.extend_from_slice(slice.as_slice())
                }
                Change::Delete { start, end } => {
                    let _ = uncompressed_data
                        .drain(..=(end - start) as usize)
                        .collect::<Vec<u8>>();
                }
                Change::Insert { data } => {
                    let slice = Self::decompress(&data)?;
                    data_raw.extend_from_slice(&slice);
                }

                Change::End { final_hash } => {
                    data_raw.shrink_to_fit();
                    self.hash = Self::hash(&data_raw);

                    let mut data: Vec<u8> = format!("blob {}\0", data_raw.len()).into_bytes();
                    data.extend_from_slice(&data_raw);

                    self.compressed_data = Self::compress(&data)?;

                    debug_assert_eq!(self.hash, final_hash);
                    if self.hash != final_hash {
                        return Err(format!(
                            "Final hash of blob {} {} is different from passed value {}",
                            self.file_path.display(),
                            Node::hash_to_hex_string(&self.hash),
                            Node::hash_to_hex_string(&final_hash)
                        ));
                    }
                }
            }
        }
        Ok(())
    }
}

impl CompressedData for LeafNode {}
impl ReadFile for LeafNode {}
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

    fn diff_file(&self, other: &Self) -> Result<Vec<Change>, String> {
        fn diff<'a>(
            v1: &'a [u8],
            v2: &'a [u8],
            v1_start: u64,
            v2_start: u64,
            other_hash: &[u8; 32],
        ) -> Result<(Change, &'a [u8], &'a [u8], u64, u64), String> {
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
                return Ok((
                    Change::Insert {
                        data: LeafNode::compress(v2)?,
                    },
                    &[],
                    &[],
                    v1_start,
                    v2_start + v2.len() as u64,
                ));
            }
            if v2.is_empty() && !v1.is_empty() {
                return Ok((
                    Change::Delete {
                        start: v1_start,
                        end: v1.len() as u64 - v1_start - 1,
                    },
                    &[],
                    &[],
                    v1_start + v1.len() as u64,
                    v2_start,
                ));
            }
            if v1.is_empty() && v2.is_empty() {
                return Ok((
                    Change::End {
                        final_hash: *other_hash,
                    },
                    &[],
                    &[],
                    v1_start,
                    v2_start,
                ));
            }

            let mut len = 0;
            while len < v1.len() && len < v2.len() && v1[len] == v2[len] {
                len += 1;
            }
            if len > 0 {
                return Ok((
                    Change::Copy {
                        start: v1_start,
                        end: v1_start + len as u64 - 1,
                    },
                    &v1[len..],
                    &v2[len..],
                    v1_start + len as u64,
                    v2_start + len as u64,
                ));
            }

            if should_delete(v1, v2) {
                let delete_len = 1;
                Ok((
                    Change::Delete {
                        start: v1_start,
                        end: v1_start + delete_len - 1,
                    },
                    &v1[delete_len as usize..],
                    v2,
                    v1_start + delete_len,
                    v2_start,
                ))
            } else {
                let mut insert_len = 1;
                while insert_len < v2.len() && v1[0] != v2[insert_len] {
                    insert_len += 1;
                }

                Ok((
                    Change::Insert {
                        data: LeafNode::compress(&v2[..insert_len])?,
                    },
                    v1,
                    &v2[insert_len..],
                    v1_start,
                    v2_start + insert_len as u64,
                ))
            }
        }

        let v1 = match LeafNode::decompress_data(&self.compressed_data) {
            Ok(leaf1_data) => leaf1_data,
            _ => return Err("Failed to decompress data of leaf1".to_string()),
        };
        let v2 = match LeafNode::decompress_data(&other.compressed_data) {
            Ok(leaf2_data) => leaf2_data,
            _ => return Err("Failed to decompress data of leaf2".to_string()),
        };
        let mut leaf1_data = v1.as_slice();
        let mut leaf2_data = v2.as_slice();

        let mut changes: Vec<Change> = Vec::new();

        let mut start_leaf1: u64 = 0;
        let mut start_leaf2: u64 = 0;
        loop {
            let change: Change;
            (change, leaf1_data, leaf2_data, start_leaf1, start_leaf2) = diff(
                leaf1_data,
                leaf2_data,
                start_leaf1,
                start_leaf2,
                &other.hash,
            )?;
            match change {
                Change::End { .. } => {
                    changes.push(change);
                    return Ok(changes);
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

        let mut raw_data = LeafNode::decompress(data)?;
        debug_assert_ne!(raw_data.len(), 0);

        raw_data.drain(0..5);

        let size: u64;
        (size, raw_data) = match Self::read_until_null(raw_data) {
            Ok((size_vec, data)) => (to_num(size_vec), data),
            Err(_) => return Err("Unable to retrieve size from data".to_string()),
        };
        debug_assert_eq!(raw_data.len() as u64, size);
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
    fn write_blob(&self, path: &Path) -> Result<(), String> {
        let dir_path = path.join(&LeafNode::hash_to_hex_string(&self.hash)[..2]);
        if !dir_path.exists() {
            match fs::create_dir_all(&dir_path) {
                Ok(_) => (),
                Err(_) => {
                    return Err(format!(
                        "Unable to create the directory {}",
                        dir_path.display()
                    ));
                }
            }
        }
        let file_path = dir_path.join(&LeafNode::hash_to_hex_string(&self.hash)[2..]);

        match self.atomic_write_file(&file_path, self.data()) {
            Ok(file) => {
                let temp_path = file.into_temp_path();
                match self.atomic_rename(&temp_path, &file_path) {
                    Ok(_) => Ok(()),
                    Err(e) => Err(format!(
                        "Unable to persist the file {} Error:{e}",
                        temp_path.display()
                    )),
                }
            }
            Err(e) => Err(e),
        }
    }

    fn is_executable(&self) -> Result<bool, String> {
        #[cfg(unix)]
        {
            let metadata = fs::metadata(&self.file_path);
            match metadata {
                Ok(file_mode) => {
                    let mode = file_mode.permissions().mode();
                    if mode & 0o111 != 0 {
                        return Ok(true);
                    }
                    Ok(false)
                }
                Err(_) => Err(format!(
                    "Unable to get metadata for file {}",
                    self.file_path.display()
                )),
            }
        }
        #[cfg(windows)]
        Ok(matches!(
            self.file_path.extension().and_then(|ext| ext.to_str()),
            Some("exe") | Some("bat") | Some("cmd") | Some("sh")
        ))
    }

    fn atomic_write_file(&self, path: &Path, data: &[u8]) -> Result<NamedTempFile, String> {
        let parent_dir = match path.parent() {
            Some(dir) => dir,
            None => {
                return Err(format!(
                    "Unable to get parent directory of file of path:{}",
                    path.display()
                ));
            }
        };
        let mut file = match NamedTempFile::new_in(parent_dir) {
            Ok(file) => file,
            Err(_) => return Err(format!("Unable to create the file {}", path.display())),
        };
        match file.write_all(data) {
            Ok(_) => (),
            Err(_) => return Err(format!("Unable to write to the file {}", path.display())),
        }
        match file.flush() {
            Ok(_) => Ok(file),
            Err(_) => Err(format!("Unable to write to the file {}", path.display())),
        }
    }

    fn atomic_rename(&self, old_path: &Path, new_path: &Path) -> Result<(), String> {
        match fs::rename(old_path, new_path) {
            Ok(_) => Ok(()),
            Err(_) => Err(format!(
                "Unable to rename file from {} to {}",
                old_path.display(),
                new_path.display()
            )),
        }
    }
}
