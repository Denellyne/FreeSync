mod node;
#[cfg(test)]
mod tests;
mod traits;

use crate::merkle::node::{LeafNode, Node, TreeNode};
use crate::merkle::traits::CompressedData;
use std::fs;
use std::fs::DirEntry;
use std::path::PathBuf;

pub struct MerkleBuilder;

impl MerkleBuilder {
    pub fn new(path: PathBuf) -> Result<Node, String> {
        match fs::read_dir(&path) {
            Ok(_) => match path {
                path if path.is_dir() => MerkleBuilder::new_tree(path),
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

    fn new_node(path: DirEntry) -> Result<Node, String> {
        match path.path() {
            path if path.is_dir() => Self::new_tree(path),
            path if path.is_file() => Self::new_leaf(path),
            _ => Err(String::from(format!(
                "Unable to generate new node, {}",
                path.path().display()
            ))),
        }
    }
    fn new_leaf(file_path: PathBuf) -> Result<Node, String> {
        match Self::hash_file(&file_path) {
            Ok((hash, data)) => Ok(Node::Leaf(LeafNode {
                hash,
                compressed_data: MerkleBuilder::compress(&data),
                file_path,
            })),
            Err(e) => Err(e),
        }
    }
    fn new_tree(dir_path: PathBuf) -> Result<Node, String> {
        let paths = fs::read_dir(&dir_path).expect("Unable to read directory");
        let mut vec: Vec<Node> = Vec::new();

        for path in paths {
            let path = path.expect("Unable to read directory entry");

            match Self::new_node(path) {
                Ok(node) => vec.push(node),
                Err(e) => return Err(format!("{} at {}", e, dir_path.display())),
            }
        }

        if !vec.is_empty() {
            vec.sort_by(|a, b| a.get_path().cmp(b.get_path()));
        }
        Ok(Node::Tree(TreeNode {
            hash: Self::hash_tree(&dir_path, &vec),
            file_path: dir_path,
            children: vec,
        }))
    }

    fn hash_file(path: &PathBuf) -> Result<([u8; 32], Vec<u8>), String> {
        let file_contents = fs::read(&path);
        match file_contents {
            Ok(contents) => {
                let hash = Self::hash(path, &contents);
                Ok((hash, contents))
            }
            _ => Err(format!("Unable to read file {}", path.display())),
        }
    }

    fn hash(path: &PathBuf, vec: &Vec<u8>) -> [u8; 32] {
        use sha2::{Digest, Sha256};

        match path.to_str() {
            Some(str) => {
                let mut data = str.as_bytes().to_owned();
                data.extend(vec);
                Sha256::digest(data).into()
            }
            None => panic!("Unable to convert path to string"),
        }
    }

    fn hash_tree(path: &PathBuf, vec: &Vec<Node>) -> [u8; 32] {
        let mut data: Vec<u8> = Vec::with_capacity(vec.len() * 32);

        for index in 0..vec.len() {
            let children_hash = vec
                .get(index)
                .expect("Invalid access to children vector,probably out of bounds")
                .get_hash();
            data.extend_from_slice(&children_hash);
        }

        MerkleBuilder::hash(&path, &data)
    }
}
impl CompressedData for MerkleBuilder {}
