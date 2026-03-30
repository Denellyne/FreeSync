use logger::Logger;
use merkle::{merklenode::traits::TreeIO, merkletree::MerkleTree};
use server_internals::server::Server;
use std::env;
use std::path::PathBuf;

fn main() {
    let mut args: Vec<String> = env::args().collect();
    assert!(args.len() >= 3, "Not enough arguments were given!");
    let mut port: String = "INVALID".to_string();
    let mut init: bool = false;
    let mut path = PathBuf::from(".");

    while args.len() > 1 {
        let arg = args.remove(1);
        if arg == "--init" {
            init = true;
        } else if arg == "--port" {
            port = args.remove(1);
        } else if arg == "--path" {
            let path_str = args.remove(1).to_owned();
            path = PathBuf::from(path_str);
        } else {
            panic!("Invalid argument")
        }
    }

    if init {
        let tree = MerkleTree::create(path.to_path_buf())
            .expect("Unable to create a repo from current directory");
        tree.save_tree().expect("Unable to write tree to disk");
    }

    let tx = Logger::create(
        "./logs/server.log",
        "Server".parse().expect("Unable to parse string"),
        true,
        true,
    );
    let server = Server::new(port, path, tx);

    server.run_server();
}
