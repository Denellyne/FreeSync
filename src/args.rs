use merkle::merklenode::node::Node;
use merkle::merklenode::traits::TreeIO;
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
use merkle::traits::{Hashable, ReadFile};
use std::env;
use std::io::{Read, Write};
use std::net::{Ipv4Addr, TcpStream};

fn display_help() {
    let strs = vec![
        "FreeSync:",
        "-h | --help Prints the help menu",
        "-b | --blob [blob hash] Decrypts blob and displays its contents",
        "--status Prints the current status of the tree",
        "--set [IP : Port] Set IP address and port",
        "--build Builds the tree",
        "--clone Clones from the IP and Port defined in the UPSTREAM_FILE",
        "--pull Pulls the updates to the server",
        "--push Pushes the diffs to the server",
        "--branch [-n|-s]) Branch command:\n\t-n [name of branch]) Creates a new branch\n\t-s [name of branch]) Switches to another branch",
    ];

    for str in strs {
        println!("{}", str);
    }
}

fn execute_commands(mut args: Vec<String>) -> Vec<String> {
    assert!(!args.is_empty());
    let opt = args.remove(0);

    match opt.as_str() {
        "-h" | "--help" => display_help(),
        "-b" | "--blob" => {
            assert!(!args.is_empty());
            let path = args.remove(0);
            let node = MerkleTree::get_blob_data(&path);
            match node {
                Ok(node) => println!("{}", node),
                Err(msg) => println!("{}", msg),
            }
        }
        "--pull" => {
            if let Err(e) = pull() {
                eprintln!("{}", e);
            }
        }
        "--build" => {
            if let Err(e) = build_tree() {
                eprintln!("{}", e);
            }
        }
        "--clone" => {
            if let Err(e) = clone() {
                eprintln!("{e}")
            }
        }

        "--set" => {
            assert!(args.len() >= 2);
            let ip = args.remove(0);
            if let Err(e) = ip.parse::<Ipv4Addr>() {
                eprintln!("{e}");
                return args;
            }
            let port = args.remove(0);
            let port = match port.parse::<u16>() {
                Ok(port) => port.to_string(),
                Err(e) => {
                    eprintln!("{e}");
                    return args;
                }
            };
            let ip = format!("{ip}:{port}");
            if let Err(e) = MerkleTree::set_upstream(".".into(), ip.to_string()) {
                eprintln!("{e}");
            }
        }
        "--status" => {
            let path = env::current_dir().unwrap();
            let branch_file = match MerkleTree::get_head_path(path.clone()) {
                Ok(t) => t,
                Err(e) => {
                    eprintln!("{}", e);
                    return args;
                }
            };
            let hash_string = MerkleTree::get_branch_hash(path)
                .expect("Unable to get hash for the current branch");

            println!("Branch:{}\nHash:{}", branch_file.display(), hash_string);
        }
        _ => eprintln!("You must provide at least one argument"),
    }
    args
}
fn build_tree() -> Result<(), String> {
    let dir = match env::current_dir() {
        Ok(dir) => dir,
        Err(e) => return Err(e.to_string()),
    };

    let node = MerkleTree::create(dir).expect("Unable to create tree");
    #[cfg(debug_assertions)]
    println!("{:?}", node);

    println!("Tree built successfully!");

    match node.save_tree() {
        Ok(_) => println!("Initialized tree and saved it successfully!"),
        Err(e) => eprintln!("{}", e),
    }
    Ok(())
}

fn clone() -> Result<(), String> {
    let dir = match env::current_dir() {
        Ok(dir) => dir,
        Err(e) => return Err(e.to_string()),
    };

    let addr = match MerkleTree::get_upstream(".".into()) {
        Ok(addr) => addr,
        Err(e) => panic!("{e}"),
    };
    let mut conn = TcpStream::connect(addr).unwrap();
    println!("Connected");

    let command = "CLONE\n\n";

    conn.write_all(command.as_bytes()).unwrap();
    println!("Wrote");

    let mut buf: Vec<u8> = Vec::new();
    conn.read_to_end(&mut buf).unwrap();
    let node = bincode::deserialize::<Node>(&buf).expect("Unable to deserialize node");
    match node {
        Node::Tree(tree_node) => {
            tree_node.deserialize().unwrap();
            tree_node.save_tree()
        }
        Node::Leaf(_) => Err("It was a leaf node".to_owned()),
    }
}
fn pull() -> Result<(), String> {
    let dir = match env::current_dir() {
        Ok(dir) => dir,
        Err(e) => return Err(e.to_string()),
    };

    let node = MerkleTree::create(dir).expect("Unable to create tree");
    println!("Tree created");
    let hash = TreeNode::hash_to_hex_string(&node.get_hash());
    let addr = match MerkleTree::get_upstream(".".into()) {
        Ok(addr) => addr,
        Err(e) => panic!("{e}"),
    };
    let mut conn = TcpStream::connect(addr).unwrap();
    println!("Connected");

    let command = "GET UPSTREAM\n\n";

    conn.write_all(command.as_bytes()).unwrap();
    println!("Wrote");

    let mut upstream_hash: String = String::new();
    conn.read_to_string(&mut upstream_hash).unwrap();
    println!("{upstream_hash}");

    Ok(())
}

pub(crate) fn parse_args(mut args: Vec<String>) {
    #[cfg(debug_assertions)]
    dbg!(&args);

    match args.len() {
        0..=1 => {
            eprintln!("You must provide at least one directory path");
            display_help();
        }
        _ => {
            args.remove(0);
            while !args.is_empty() {
                args = execute_commands(args);
            }
        }
    };
}
