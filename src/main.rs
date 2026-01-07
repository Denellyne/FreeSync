mod merkle;
use crate::merkle::MerkleBuilder;
use std::env;
use std::path::PathBuf;

fn main() {
    let mut args: Vec<String> = env::args().collect();
    #[cfg(debug_assertions)]
    dbg!(&args);

    match args.len() {
        1 => println!("You must provide at least one directory path"),
        _ => {
            let dir: String = args.remove(1);
            let _node = MerkleBuilder::new(PathBuf::from(dir)).expect("Unable to create tree");
            #[cfg(debug_assertions)]
            println!("{:?}", _node);

            println!("Tree built successfully!");
        }
    }
}

/*
Todo
Compare trees and get all different points
Create Server
*/
