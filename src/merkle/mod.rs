mod diff;
mod leafnode;
mod node;

pub mod traits;
pub mod treenode;

#[cfg(test)]
mod tests;

use crate::merkle::node::{LeafNode, Node, TreeNode};
use crate::merkle::traits::{CompressedData, Hashable, HashableNode, IO, LeafData, EntryData};
use std::collections::HashSet;
use std::fs;
use std::fs::DirEntry;
use std::path::{Path, PathBuf};

pub struct MerkleTree;

enum MerkleEntry{
    Blob{hash:[u8; 32],file_name:String,mode:&'static [u8;6]},
    Tree{hash:[u8; 32],file_name:String,entries: Vec<MerkleEntry>},
}

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

        fn read_until_null(data: &[u8]) -> (&[u8], &[u8]) {
            if let Some(pos) = data.iter().position(|&b| b == 0) {
                ( &data[..pos], &data[pos + 2..])
            } else {
                (data, &[]) // no null found, return everything
            }
        }

        let path = path.as_ref();
        let mut entries: Vec<MerkleEntry>  = Vec::new();

        let data = Self::read_file(path)?;
        while !data.is_empty(){
            let mode : &[u8;6] =  &data[..=6].try_into().expect("Error converting to slice");
            if !mode.eq(Self::REGULAR_FILE) && !mode.eq(Self::EXECUTABLE_FILE) && !mode.eq(Self::DIRECTORY) {
                return Err(format!("Invalid mode: {}", String::from_utf8_lossy(mode)));
            }
            let data = &data[7..];
            let (file_name,mut data) = read_until_null(data);
            let hash_vec : [u8;32]  =data.split_off(..32).expect("Unable to read blob").to_vec().try_into().expect("Unable to convert blob into a 32 byte array");
            let file_name = String::from_utf8_lossy(file_name).to_string();
            let mode = match mode{
                Self::REGULAR_FILE => Self::REGULAR_FILE,
                Self::EXECUTABLE_FILE => Self::EXECUTABLE_FILE,
                Self::DIRECTORY => Self::DIRECTORY,
                _ => return Err("Invalid mode parsed".to_string()),
            };
            entries.push(MerkleEntry::Blob{hash:hash_vec,file_name,mode });
        }














        todo!()

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
    fn new_leaf(file_path: PathBuf) -> Result<LeafNode, String> {
        match Self::hash_file(&file_path) {
            Ok((hash, data)) => Ok(LeafNode {
                hash,
                compressed_data: MerkleTree::compress(&data),
                file_path,
            }),
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

    fn from_blob(path: impl AsRef<Path>) -> Result<LeafNode, String> {
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
impl EntryData for MerkleTree{}
impl EntryData for MerkleEntry{}
