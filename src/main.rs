mod merkle;
use crate::merkle::Node;
use std::env;

fn main() {
    let mut args: Vec<String> = env::args().collect();
    #[cfg(debug_assertions)]
    dbg!(&args);

    match args.len() {
        1 => println!("You must provide at least one directory path"),
        _ => {
            let dir: String = args.remove(1);
            let node = Node::new_tree(dir).expect("Unable to create tree");
            #[cfg(debug_assertions)]
            println!("{:?}", node);

            println!("Tree built successfully!");
        }
    }
}

/*
Todo
Compare trees and get all different points
Create Server
*/
