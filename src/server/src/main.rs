use logger::Logger;
use merkle::merkletree::MerkleTree;
use server_internals::server::Server;
use std::env;
use std::path::PathBuf;

fn display_help() {
    let strs = vec![
        "FreeSync Server:",
        "-h | --help Prints the help menu",
        "--init Inits the .freesync folder on startup",
        "--port Specifies the port used",
    ];

    for str in strs {
        println!("{}", str);
    }
}

fn main() {
    let mut args: Vec<String> = env::args().collect();
    if args.len() <= 1 {
        eprintln!("Not enough arguments were given!");
        display_help();
        return;
    }

    let mut port: String = "INVALID".to_string();
    let mut init: bool = false;
    let mut path = PathBuf::from(".");

    while args.len() > 1 {
        let arg = args.remove(1);
        if arg == "--init" {
            init = true;
        } else if arg == "--port" {
            port = args.remove(1);
        } else if arg == "--help" || arg == "-h" {
            display_help();
            return;
        } else if arg == "--path" {
            let path_str = args.remove(1).to_owned();
            path = PathBuf::from(path_str);
        } else {
            eprintln!("Unknown argument: {}", arg);
            display_help();
            return;
        }
    }

    if init {
        MerkleTree::init(path.to_path_buf(), args.remove(0))
            .expect("Unable to create a repo from current directory");
    }

    let tx = Logger::create(
        "./logs/server.log",
        "Server".parse().expect("Unable to parse string"),
        true,
        true,
    );
    let server = Server::new(port, path, tx);

    server.run_server(4);
}
