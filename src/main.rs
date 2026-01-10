mod merkle;
use crate::merkle::merklenode::traits::TreeIO;
use crate::merkle::merkletree::MerkleTree;
use std::env;
use std::path::PathBuf;

fn execute_commands(mut args: Vec<String>) -> Vec<String> {
    let opt = args.remove(1);

    match opt.as_str() {
        "-b" => {
            let path = args.remove(1);
            let node = MerkleTree::get_blob_data(&path);
            match node {
                Ok(node) => println!("{}", node),
                Err(msg) => println!("{}", msg),
            }
        }
        _ => eprintln!("You must provide at least one argument"),
    }
    args
}
fn build_tree(mut args: Vec<String>) -> Vec<String> {
    let dir: String = args.remove(1);

    let node = MerkleTree::create(PathBuf::from(dir)).expect("Unable to create tree");
    #[cfg(debug_assertions)]
    println!("{:?}", node);

    println!("Tree built successfully!");

    match node.save_tree() {
        true => println!("Initialized tree and saved it successfully!"),
        false => eprintln!("Failed to initialize tree"),
    }
    args
}

fn main() {
    let args: Vec<String> = env::args().collect();
    #[cfg(debug_assertions)]
    dbg!(&args);

    match args.len() {
        0..=1 => eprintln!("You must provide at least one directory path"),
        3 => {
            execute_commands(args);
        }
        _ => {
            build_tree(args);
        }
    }
}

/*
Todo
Create Server
allow symbolic links or not
*/
