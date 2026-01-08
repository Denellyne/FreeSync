mod merkle;
use crate::merkle::MerkleBuilder;
use std::env;
use std::path::PathBuf;
use crate::merkle::traits::TreeIO;

fn main() {
    let mut args: Vec<String> = env::args().collect();
    #[cfg(debug_assertions)]
    dbg!(&args);

    match args.len() {
        1 => println!("You must provide at least one directory path"),
        _ => {
            let dir: String = args.remove(1);
            let node = MerkleBuilder::new(PathBuf::from(dir)).expect("Unable to create tree");
            #[cfg(debug_assertions)]
            println!("{:?}", node);

            println!("Tree built successfully!");
            
            match node.write_tree() {
                true => println!("Initialized tree"),
                false => println!("Failed to initialize tree")
            }

        }
    }
}

/*
Todo
Compare trees and get all different points
Create Server
*/
