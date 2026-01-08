use std::fs;
use crate::merkle::node::{Node, TreeNode};
use crate::merkle::traits::TreeIO;
use std::path::PathBuf;

impl TreeIO for TreeNode {

    fn init() -> bool{

        let main_dir = fs::create_dir(PathBuf::from(Self::MAIN_FOLDER));
        match main_dir {
            Ok(_)  => true,
            Err(e) => {
                eprintln!("Error creating tree directory: {}", e);
                false
            }
        };
        let obj_dir = fs::create_dir(PathBuf::from(Self::OBJ_FOLDER));
        match obj_dir {
            Ok(_)  => true,
            Err(e) => {
                eprintln!("Error creating tree directory: {}", e);
                false
            }
        };


        true
    }

    fn write_tree(&self) -> bool {
        Self::init()
    }

    fn read_tree(path: &PathBuf) -> Result<Self,String> {
        todo!()
    }
}
