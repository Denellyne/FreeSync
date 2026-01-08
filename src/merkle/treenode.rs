use crate::merkle::node::{Node, TreeNode};
use crate::merkle::traits::TreeIO;
use crate::merkle::traits::internal_traits::TreeIOInternal;
use crate::merkle::traits::{Hashable, HashableNode, LeafIO};
use std::fs;
use std::path::{Path, PathBuf};

impl Hashable for TreeNode {
    fn get_hash(&self) -> [u8; 32] {
        self.hash
    }
}
impl HashableNode for TreeNode {
    fn hash_tree(path: &Path, children: &[Node]) -> [u8; 32] {
        let mut data: Vec<u8> = Vec::with_capacity(children.len() * 32);

        for index in 0..children.len() {
            let children_hash = children
                .get(index)
                .expect("Invalid access to children vector,probably out of bounds")
                .get_hash();
            data.extend_from_slice(&children_hash);
        }

        <Node as Hashable>::hash(path, data.as_slice())
    }
}

impl TreeIO for TreeNode {
    fn save_tree(&self) -> bool {
        if !self.init() {
            eprintln!("Unable to init tree directory");
            return false;
        }
        if !self.write_tree() {
            eprintln!("Unable to write tree file");
            return false;
        }

        true
    }

    fn read_tree(path: impl AsRef<Path>) -> Result<Self, String>
    where
        Self: Sized,
    {
        todo!()
    }
}

impl TreeIOInternal for TreeNode {
    fn init(&self) -> bool {
        let paths = [Self::MAIN_FOLDER, Self::OBJ_FOLDER];
        for path in paths.iter() {
            let path = PathBuf::from(path);

            if !path.exists() {
                let obj_dir = fs::create_dir_all(path);

                match obj_dir {
                    Ok(_) => true,
                    Err(e) => {
                        eprintln!("{}", e);
                        false
                    }
                };
            }
        }

        self.write_file(Self::HEAD_FILE, self.hash)
    }

    fn write_tree(&self) -> bool {
        let path = PathBuf::from(Self::OBJ_FOLDER).join(&Self::hash_to_hex_string(&self.hash)[..2]);
        if !path.exists() {
            fs::create_dir_all(&path).expect("Failed to create tree dir");
        }
        let parent_file = path.join(&Self::hash_to_hex_string(&self.hash)[2..]);

        for child in &self.children {
            match child {
                Node::Leaf(child) => {
                    if !child.write_blob(Self::OBJ_FOLDER.as_ref()) {
                        eprintln!("Error writing blob to disk: {}", child.file_path.display());
                        return false;
                    }
                }
                Node::Tree(child) => {
                    if !child.write_tree() {
                        eprintln!("Error writing tree to disk: {}", child.file_path.display());
                        return false;
                    }
                }
            }
            self.write_file(&parent_file, child.get_hash());
        }

        true
    }
    fn read_tree(path: impl AsRef<Path>) -> Result<Self, String> {
        todo!()
    }
}
