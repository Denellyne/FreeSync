use crate::merkle::merklenode::leaf::LeafNode;
use crate::merkle::merklenode::node::Node;
use crate::merkle::merklenode::traits::LeafData;
use crate::merkle::merklenode::tree::TreeNode;
use crate::merkle::traits::{CompressedData, Hashable, IO};
use std::fs;
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
    pub fn from(path: impl AsRef<Path>, real_path: PathBuf) -> Result<Node, String> {
        Node::from(path, real_path)
    }

    pub(super) fn new_leaf(file_path: PathBuf) -> Result<LeafNode, String> {
        LeafNode::new(file_path)
    }

    pub(super) fn new_tree(dir_path: PathBuf) -> Result<TreeNode, String> {
        TreeNode::new(dir_path)
    }

    pub fn get_blob_data(path: impl AsRef<Path>) -> Result<String, String> {
        match LeafNode::from(path.as_ref(), "".to_string().into()) {
            Ok(node) => {
                let hash = Node::hash_to_hex_string(&node.hash);
                let data = MerkleTree::decompress(node.data());
                let data = String::from_utf8_lossy(&data);

                Ok(format!("Data:{}\nHash:{}\n", data, hash))
            }
            Err(e) => Err(e),
        }
    }
}
impl CompressedData for MerkleTree {}
impl IO for MerkleTree {}
