use crate::data::Packet;
use crate::merklenode::leaf::LeafNode;
use crate::merklenode::node::Node;
use crate::merklenode::traits::{LeafIO, TreeIO};
use crate::merklenode::tree::TreeNode;
use crate::traits::{CompressedData, Hashable, IO, ReadFile};
use std::fs;
use std::path::{Path, PathBuf};

pub struct MerkleTree;

impl MerkleTree {
    fn create_setter() -> MerkleTree {
        MerkleTree
    }
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

    fn new_tree(dir_path: PathBuf) -> Result<TreeNode, String> {
        TreeNode::new(dir_path)
    }

    pub fn get_upstream(dir_path: PathBuf) -> Result<String, String> {
        let path = dir_path.join(TreeNode::UPSTREAM_FILE);
        match MerkleTree::read_file(path) {
            Ok(data) => match String::from_utf8(data) {
                Ok(data) => Ok(data),
                Err(e) => Err(e.to_string()),
            },
            Err(e) => Err(e),
        }
    }

    pub fn set_upstream(dir_path: PathBuf, ip: String) -> Result<(), String> {
        let setter = MerkleTree::create_setter();
        let path = dir_path.join(TreeNode::MAIN_FOLDER);
        if !path.exists() {
            match fs::create_dir_all(&path) {
                Ok(_) => (),
                Err(_) => return Err("Unable to create object folder".to_owned()),
            }
        }
        let path = dir_path.join(TreeNode::UPSTREAM_FILE);

        match setter.write_file(path, ip) {
            true => Ok(()),
            false => Err("Unable to set upstream".to_owned()),
        }
    }
    pub fn write_packet(dir_path: PathBuf, packet: Packet) -> Result<(), String> {
        match packet {
            Packet::ObjectFile(data, hash) => {
                let path = dir_path
                    .join(TreeNode::OBJ_FOLDER)
                    .join(&hash[..2])
                    .join(&hash[2..]);
                match MerkleTree.write_file(&path, &data) {
                    true => Ok(()),
                    false => Err("Unable to write object file".to_owned()),
                }
            }
            Packet::HeadFile(data) => {
                let path = dir_path.join(TreeNode::HEAD_FILE);
                match MerkleTree.write_file(&path, &data) {
                    true => Ok(()),
                    false => Err("Unable to write head file".to_owned()),
                }
            }
            Packet::BranchFile(hash, name) => {
                let path = dir_path.join(TreeNode::BRANCH_FOLDER).join(name);
                match MerkleTree.write_file(&path, &hash) {
                    true => Ok(()),
                    false => Err("Unable to write branch file".to_owned()),
                }
            }
        }
    }

    pub fn get_objects(dir_path: PathBuf) -> Result<Vec<Packet>, String> {
        let dirs = match fs::read_dir(dir_path) {
            Ok(dirs) => dirs,
            Err(_) => return Err("Unable to read directory".to_owned()),
        };
        let mut vec: Vec<Packet> = Vec::with_capacity(dirs.size_hint().0);
        for dir in dirs {
            let dir = match dir {
                Ok(dir) => dir,
                Err(_) => return Err("Unable to read directory".to_owned()),
            };

            let files = match fs::read_dir(dir.path()) {
                Ok(files) => files,
                Err(_) => return Err("Unable to read directory".to_owned()),
            };

            for file in files {
                let file = match file {
                    Ok(file) => file,
                    Err(_) => return Err("Unable to read directory".to_owned()),
                };
                let data = MerkleTree::read_file(file.path())?;
                let path = dir.path().join(file.path());
                vec.push(Packet::ObjectFile(data, path.display().to_string()));
            }
        }

        Ok(vec)
    }

    pub fn get_head_path(path: PathBuf) -> Result<PathBuf, String> {
        let head_file = path.join(TreeNode::HEAD_FILE);
        let head = match MerkleTree::read_file(&head_file) {
            Ok(it) => it,
            Err(_) => return Err(format!("Unable to read file:{}", head_file.display())),
        };
        Ok(path
            .to_path_buf()
            .join(TreeNode::BRANCH_FOLDER)
            .join(String::from_utf8_lossy(&head).to_string()))
    }

    pub fn get_branch_hash(path: PathBuf) -> Result<String, String> {
        let mut hash: [u8; 32] = [0; 32];
        let data: Vec<u8> = match MerkleTree::read_file(path) {
            Ok(data) => data[..32].to_vec(),
            Err(e) => return Err(e.to_string()),
        };
        hash.copy_from_slice(&data);

        Ok(hash
            .iter()
            .map(|b| format!("{:02x}", b))
            .collect::<String>())
    }

    pub fn get_branch_hash_str(path: PathBuf) -> Result<String, String> {
        let mut hash: [u8; 32] = [0; 32];
        let data: Vec<u8> = match MerkleTree::read_file(path) {
            Ok(data) => data[..32].to_vec(),
            Err(e) => return Err(e.to_string()),
        };
        hash.copy_from_slice(&data);
        Ok(TreeNode::hash_to_hex_string(&hash))
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

#[cfg(test)]
impl MerkleTree {
    pub(super) fn new_leaf(file_path: PathBuf) -> Result<LeafNode, String> {
        LeafNode::new(file_path)
    }
}

impl CompressedData for MerkleTree {}
impl ReadFile for MerkleTree {}
impl IO for MerkleTree {}
