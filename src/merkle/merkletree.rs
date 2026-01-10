use crate::merkle::entry::MerkleEntry;
use crate::merkle::node::node::{LeafNode, Node, TreeNode};
use crate::merkle::traits::internal_traits::TreeIOInternal as _;
use crate::merkle::traits::{
    CompressedData, EntryData, Hashable, HashableNode as _, IO, LeafData as _,
};
use std::collections::HashSet;
use std::fs;
use std::fs::DirEntry;
use std::path::{Path, PathBuf};

pub struct MerkleTree;

impl MerkleTree {
    pub fn create(path: PathBuf) -> Result<TreeNode, String> {
        match fs::read_dir(&path) {
            Ok(_) => match path {
                path if path.is_dir() => MerkleTree::new_tree(path),
                path if path.is_file() => Err(format!("Path is of a file: {}", path.display())),
                path if path.is_symlink() => Err(format!("Path is a symlink: {}", path.display())),
                _ => Err(String::from("Unable to generate merkle tree")),
            },
            _ => Err(format!(
                "Could not read directory {:?}, is it a path to a directory?",
                &path
            ))?,
        }
    }
    pub fn from(path: impl AsRef<Path>) -> Result<Node, String> {
        let path = path.as_ref();

        let head_file = path.join(TreeNode::HEAD_FILE);
        let mut data: Vec<u8> = MerkleTree::get_head_data(head_file)?;

        let path = path.join(TreeNode::OBJ_FOLDER);
        let mut entries: Vec<MerkleEntry> = Vec::new();

        while !data.is_empty() {
            let entry: MerkleEntry;
            (data, entry) = match MerkleTree::get_entry(&path, data, "".to_string()) {
                Ok((data, entry)) => (data, entry),
                Err(e) => return Err(e),
            };
            entries.push(entry);
        }

        todo!()
    }

    fn get_entry(
        path: impl AsRef<Path>,
        mut data: Vec<u8>,
        file_name: String,
    ) -> Result<(Vec<u8>, MerkleEntry), String> {
        let mode: &'static [u8; 6];
        (data, mode) = match MerkleTree::get_entry_mode(data) {
            Ok((data, mode)) => (data, mode),
            Err(e) => return Err(e),
        };

        match mode {
            Self::REGULAR_FILE | Self::EXECUTABLE_FILE => {
                let blob_data: Vec<u8>;
                let size: u64;
                (data, blob_data, size) = match MerkleTree::parse_blob(data) {
                    Ok((data, hash, size)) => (data, hash, size),
                    Err(e) => return Err(e),
                };

                Ok((
                    data,
                    MerkleEntry::Blob {
                        data: blob_data,
                        size,
                        file_name,
                        mode,
                    },
                ))
            }
            Self::DIRECTORY => {
                let file_name: String;
                let hash_vec: [u8; 32];
                (data, hash_vec, file_name) = match MerkleTree::parse_tree(data) {
                    Ok((data, hash, size)) => (data, hash, size),
                    Err(e) => return Err(e),
                };
                Ok((
                    data,
                    MerkleEntry::Tree {
                        hash: hash_vec,
                        file_name,
                        entries: vec![],
                    },
                ))
            }
            _ => Err(format!("Invalid mode: {:?}\n", mode)),
        }
    }

    fn get_entry_mode(mut data: Vec<u8>) -> Result<(Vec<u8>, &'static [u8; 6]), String> {
        let mode: [u8; 6] = match data.drain(0..6).collect::<Vec<u8>>().try_into() {
            Ok(mode) => mode,
            Err(_) => return Err("Unable to convert Vector to sized array".to_string()),
        };
        match &mode {
            Self::REGULAR_FILE => Ok((data, Self::REGULAR_FILE)),
            Self::EXECUTABLE_FILE => Ok((data, Self::EXECUTABLE_FILE)),
            Self::DIRECTORY => Ok((data, Self::DIRECTORY)),
            _ => Err(format!("Invalid mode: {:?}\n", mode)),
        }
    }

    fn read_until_null(mut data: Vec<u8>) -> Result<(Vec<u8>, Vec<u8>), String> {
        if let Some(pos) = data.iter().position(|&b| b == 0) {
            let head: Vec<u8> = data.drain(0..pos).collect();
            data.drain(0..1);
            return Ok((head, data));
        }
        Err("Unable to read until null-byte".to_owned())
    }

    fn parse_blob(mut data: Vec<u8>) -> Result<(Vec<u8>, Vec<u8>, u64), String> {
        let size: u64;
        (size, data) = match MerkleTree::read_until_null(data.to_vec()) {
            Ok((size, data)) => {
                let s: String = size.iter().map(|n| n.to_string()).collect();
                let num: u64 = s.parse().expect("Unable to convert vector to num");
                (num, data)
            }
            Err(err) => return Err(err),
        };
        let blob_data: Vec<u8>;
        (blob_data, data) = match MerkleTree::read_until_null(data.to_vec()) {
            Ok((blob_data, data)) => (blob_data, data),
            Err(err) => return Err(err),
        };

        Ok((data, blob_data, size))
    }
    fn parse_tree(mut data: Vec<u8>) -> Result<(Vec<u8>, [u8; 32], String), String> {
        let file_name: Vec<u8>;

        (file_name, data) = match MerkleTree::read_until_null(data.to_vec()) {
            Ok((file_name, data)) => (file_name, data),
            Err(err) => return Err(err),
        };
        let hash_vec: [u8; 32] = match data.drain(0..32).collect::<Vec<u8>>().try_into() {
            Ok(it) => it,
            Err(_) => return Err("Unable to convert Vector to sized array".to_string()),
        };

        let file_name = String::from_utf8_lossy(&file_name).to_string();
        Ok((data, hash_vec, file_name))
    }

    fn get_head_data(path: impl AsRef<Path>) -> Result<Vec<u8>, String> {
        let path = path.as_ref();

        let head_file = path.join(TreeNode::HEAD_FILE);
        let data = match Self::read_file(head_file)?.try_into() {
            Ok(it) => it,
            Err(_) => return Err(format!("Unable to read file:{}", path.display())),
        };

        let path = path.join(TreeNode::OBJ_FOLDER);

        let header = Node::hash_to_hex_string(&data);
        let data_path = path.join(header);

        Self::read_file(data_path)
    }

    fn new_node(path: DirEntry) -> Result<Node, String> {
        match path.path() {
            path if path.is_dir() => Ok(Node::Tree(Self::new_tree(path)?)),
            path if path.is_file() => Ok(Node::Leaf(Self::new_leaf(path)?)),
            _ => Err(format!(
                "Unable to generate new node, {}",
                path.path().display()
            )),
        }
    }
    pub(super) fn new_leaf(file_path: PathBuf) -> Result<LeafNode, String> {
        match Self::hash_file(&file_path) {
            Ok((hash, data_raw)) => {
                let mut data: Vec<u8> = format!("blob {}\0", data_raw.len()).into_bytes();
                data.extend_from_slice(&data_raw);
                Ok(LeafNode {
                    hash,
                    compressed_data: MerkleTree::compress(&data),
                    file_path,
                })
            }
            Err(e) => Err(e),
        }
    }
    fn new_tree(dir_path: PathBuf) -> Result<TreeNode, String> {
        let paths = Self::read_dir(&dir_path);
        let paths = paths?;
        let mut vec: Vec<Node> = Vec::new();

        let filter: HashSet<_> = HashSet::from([".freesync"]);
        'pathLoop: for path in paths {
            let path = path.expect("Unable to read directory entry");

            for str in filter.iter().collect::<Vec<_>>() {
                if path
                    .file_name()
                    .to_str()
                    .expect("Unable to convert to string")
                    .contains(str)
                {
                    continue 'pathLoop;
                }
            }

            match Self::new_node(path) {
                Ok(node) => vec.push(node),
                Err(e) => return Err(format!("{} at {}", e, dir_path.display())),
            }
        }

        if !vec.is_empty() {
            vec.sort_by(|a, b| a.get_path().cmp(b.get_path()));
        }
        Ok(TreeNode {
            hash: TreeNode::hash_tree(&vec),
            file_path: dir_path,
            children: vec,
        })
    }

    fn hash_file(path: impl AsRef<Path>) -> Result<([u8; 32], Vec<u8>), String> {
        let file_contents = Self::read_file(&path);
        match file_contents {
            Ok(contents) => {
                let hash = Node::hash(&contents);
                Ok((hash, contents))
            }
            _ => Err(format!("Unable to read file {}", path.as_ref().display())),
        }
    }

    pub(super) fn from_blob(path: impl AsRef<Path>) -> Result<LeafNode, String> {
        match fs::read(&path) {
            Ok(data) => {
                let uncompressed = Self::decompress(&data);
                let hash = Node::hash(&uncompressed);
                Ok(LeafNode {
                    file_path: path.as_ref().to_path_buf(),
                    hash,
                    compressed_data: data,
                })
            }
            Err(e) => Err(e.to_string()),
        }
    }

    pub fn get_blob_data(path: impl AsRef<Path>) -> Result<String, String> {
        match Self::from_blob(path) {
            Ok(node) => {
                let hash = Node::hash_to_hex_string(&node.hash);
                let data = MerkleTree::decompress(node.data());
                let data = match String::from_utf8(data.clone()) {
                    Ok(data) => data,
                    _ => data.iter().map(|b| format!("{:02x}", b)).collect(),
                };

                Ok(format!("Data:{}\nHash:{}\n", data, hash))
            }
            Err(e) => Err(e),
        }
    }
}
impl CompressedData for MerkleTree {}
impl IO for MerkleTree {}
impl EntryData for MerkleTree {}
impl EntryData for MerkleEntry {}
