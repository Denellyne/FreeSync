use crate::merkle::node::node::{Node, TreeNode};
use crate::merkle::traits::internal_traits::TreeIOInternal;
use crate::merkle::traits::{EntryData, TreeIO};
use crate::merkle::traits::{Hashable, HashableNode, LeafIO};
use std::fs;
use std::path::Path;

impl EntryData for TreeNode {}

impl Hashable for TreeNode {
    fn hash(vec: &[u8]) -> [u8; 32] {
        use sha2::{Digest, Sha256};
        Sha256::digest(vec).into()
    }

    fn get_hash(&self) -> [u8; 32] {
        self.hash
    }
}
impl HashableNode for TreeNode {
    fn hash_tree(children: &[Node]) -> [u8; 32] {
        let mut data: Vec<u8> = Vec::with_capacity(children.len() * 32);

        for child in children.iter() {
            let children_hash = child.get_hash();
            data.extend_from_slice(&children_hash);
        }

        <Node as Hashable>::hash(data.as_slice())
    }
}

impl TreeIO for TreeNode {
    fn save_tree(&self) -> bool {
        if !self.init() {
            eprintln!("Unable to init tree directory");
            return false;
        }
        if !self.write_tree(&self.file_path) {
            eprintln!("Unable to write tree file");
            return false;
        }

        true
    }
}

impl TreeIOInternal for TreeNode {
    fn init(&self) -> bool {
        let paths = [Self::MAIN_FOLDER, Self::OBJ_FOLDER];
        for path in paths.iter() {
            let path = self.file_path.join(path);

            if !path.exists() && fs::create_dir_all(path).is_err() {
                eprintln!("Unable to create new tree directory");
                return false;
            }
        }

        self.write_file(self.file_path.join(Self::HEAD_FILE), self.hash)
    }

    fn write_tree(&self, cwd: impl AsRef<Path>) -> bool {
        let cwd = cwd.as_ref().to_path_buf();
        let obj_folder = cwd.join(Self::OBJ_FOLDER);
        let path = cwd
            .join(Self::OBJ_FOLDER)
            .join(&Self::hash_to_hex_string(&self.hash)[..2]);
        if !path.exists() {
            fs::create_dir_all(&path).expect("Failed to create tree dir");
        }

        let parent_file = path.join(&Self::hash_to_hex_string(&self.hash)[2..]);
        let mut data: Vec<u8> = Vec::new();
        for child in self.children.iter() {
            let filename = child.get_filename();
            let entry = match child {
                Node::Leaf(child) => {
                    if !child.write_blob(&obj_folder) {
                        eprintln!("Error writing blob to disk: {}", child.file_path.display());
                        return false;
                    }
                    match child.is_executable() {
                        true => Self::EXECUTABLE_FILE.as_slice(),
                        false => Self::REGULAR_FILE.as_slice(),
                    }
                }
                Node::Tree(child) => {
                    if !child.write_tree(&cwd) {
                        eprintln!("Error writing tree to disk: {}", child.file_path.display());
                        return false;
                    }
                    Self::DIRECTORY.as_slice()
                }
            };

            data.extend_from_slice(entry);
            data.push(b' ');
            data.extend_from_slice(filename.as_bytes());
            data.push(0);
            data.extend_from_slice(&child.get_hash());
        }
        self.write_file(&parent_file, data);
        true
    }
}
