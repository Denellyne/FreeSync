mod diff;
mod leafnode;
mod node;

pub mod traits;
pub mod treenode;

#[cfg(test)]
mod tests;

use crate::merkle::node::{LeafNode, Node, TreeNode};
use crate::merkle::traits::{CompressedData, IO, TreeIO};
use std::collections::HashSet;
use std::fs;
use std::fs::{DirEntry};
use std::path::{Path, PathBuf};

pub struct MerkleTree;

impl MerkleTree {
    pub fn new(path: PathBuf) -> Result<TreeNode, String> {
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
    pub fn from(path: PathBuf) -> Result<Node, String> {
        match TreeNode::read_tree(&path) {
            Ok(node) => Ok(Node::Tree(node)),
            Err(_) => Err(format!("Unable to read tree: {}", path.display())),
        }
    }

    fn new_node(path: DirEntry) -> Result<Node, String> {
        match path.path() {
            path if path.is_dir() => Ok(Node::Tree(Self::new_tree(path)?)),
            path if path.is_file() => Self::new_leaf(path),
            _ => Err(format!(
                "Unable to generate new node, {}",
                path.path().display()
            )),
        }
    }
    fn new_leaf(file_path: PathBuf) -> Result<Node, String> {
        match Self::hash_file(&file_path) {
            Ok((hash, data)) => Ok(Node::Leaf(LeafNode {
                hash,
                compressed_data: MerkleTree::compress(&data),
                file_path,
            })),
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
            hash: Self::hash_tree(&dir_path, &vec),
            file_path: dir_path,
            children: vec,
        })
    }

    fn hash_file(path: &PathBuf) -> Result<([u8; 32], Vec<u8>), String> {
        let file_contents = Self::read_file(path);
        match file_contents {
            Ok(contents) => {
                let hash = Self::hash(path, &contents);
                Ok((hash, contents))
            }
            _ => Err(format!("Unable to read file {}", path.display())),
        }
    }
    fn hash(path: &Path, vec: &[u8]) -> [u8; 32] {
        use sha2::{Digest, Sha256};

        match path.to_str() {
            Some(str) => {
                let mut data = str.as_bytes().to_owned();
                data.extend(vec);
                Sha256::digest(&data).into()
            }
            None => panic!("Unable to convert path to string"),
        }
    }
    fn hash_tree(path: &Path, vec: &[Node]) -> [u8; 32] {
        let mut data: Vec<u8> = Vec::with_capacity(vec.len() * 32);

        for index in 0..vec.len() {
            let children_hash = vec
                .get(index)
                .expect("Invalid access to children vector,probably out of bounds")
                .get_hash();
            data.extend_from_slice(&children_hash);
        }

        MerkleTree::hash(path, &data)
    }
}
impl CompressedData for MerkleTree {}
impl IO for MerkleTree {}
