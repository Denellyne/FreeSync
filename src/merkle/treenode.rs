use crate::merkle::node::{Node, TreeNode};
use crate::merkle::traits::{LeafIO, TreeIO};
use std::fs;
use std::path::PathBuf;

impl TreeIO for TreeNode {
    fn init(&self) -> bool {
        let path = PathBuf::from(Self::OBJ_FOLDER);
        if path.exists() {
            return true;
        }

        let obj_dir = fs::create_dir_all(path);

        match obj_dir {
            Ok(_) => true,
            Err(e) => {
                eprintln!("{}", e);
                false
            }
        };

        true
    }

    fn write_tree(&self) -> bool {
        let path = PathBuf::from(Self::OBJ_FOLDER).join(Node::get_hash_string(self.hash));
        if !path.exists() {
            fs::create_dir(&path).expect("Failed to create tree dir");
        }

        for child in &self.children {
            match child {
                Node::Leaf(child) => {
                    if !child.write_blob(&path) {
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
        }

        true
    }

    fn read_tree(path: &PathBuf) -> Result<Self, String> {
        todo!()
    }
}
