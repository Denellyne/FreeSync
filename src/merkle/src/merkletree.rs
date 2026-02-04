use std::fs;
use std::path::{Path, PathBuf};
use crate::merklenode::leaf::LeafNode;
use crate::merklenode::node::Node;
use crate::merklenode::traits::{LeafData, TreeIO};
use crate::merklenode::tree::TreeNode;
use crate::traits::{CompressedData, Hashable, ReadFile};

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
    pub fn get_head_path(path: impl AsRef<Path>) -> Result<PathBuf, String> {
        let path = path.as_ref();

        let head_file = path.join(TreeNode::HEAD_FILE);
        let head = match fs::read(head_file) {
            Ok(it) => it,
            Err(_) => return Err(format!("Unable to read file:{}", path.display())),
        };
        Ok(path
            .to_path_buf()
            .join(TreeNode::BRANCH_FOLDER)
            .join(String::from_utf8_lossy(&head).to_string()))
    }

    pub fn get_blob_data(path: impl AsRef<Path>) -> Result<String, String> {
        match LeafNode::from(path.as_ref(), "".to_string().into()) {
            Ok(node) => {
                let hash = Node::hash_to_hex_string(&node.hash);
                let data = MerkleTree::decompress(node.data())?;
                let data = String::from_utf8_lossy(&data);

                Ok(format!("Data:{}\nHash:{}\n", data, hash))
            }
            Err(e) => Err(e),
        }
    }
}
impl CompressedData for MerkleTree {}
impl ReadFile for MerkleTree {}
