use crate::merkle::diff::Change;
use crate::merkle::node::LeafNode;
use crate::merkle::traits::{CompressedData, Hashable, LeafData, LeafIO};
use std::fs;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::Path;

impl CompressedData for LeafNode {}
impl Hashable for LeafNode{
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

        let v1 = LeafNode::decompress(&self.compressed_data);
        let v2 = LeafNode::decompress(&other.compressed_data);
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

        file.write_all(self.data()).expect("Unable to write data");
        file.flush().expect("Unable to flush data");
        true
    }
}
