use crate::client::Client;
use merkle::merkletree::MerkleTree;
use std::env;
use std::net::IpAddr;
use url::Url;
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
        // "--pull" => {
        //     if let Err(e) = Client::pull() {
        //         eprintln!("{}", e);
        //     }
        // }
        "--build" => {
            if let Err(e) = build_tree() {
                eprintln!("{}", e);
            }
        }
        "--clone" => {
            if let Err(e) = Client::clone() {
                eprintln!("{e}")
            }
        }

        "--set" => {
            if let Err(e) = set_upstream(&mut args) {
                eprintln!("{e}")
            }
        }
        "--status" => {
            if let Err(e) = status() {
                eprintln!("{e}")
            }
        }
        _ => eprintln!("You must provide at least 1 argument"),
    }
    args
}
fn build_tree() -> Result<(), String> {
    let dir = match env::current_dir() {
        Ok(dir) => dir,
        Err(e) => return Err(e.to_string()),
    };

    MerkleTree::apply_branch(dir, 3)
}
fn is_valid_ip(input: String) -> Result<String, String> {
    if input == "localhost" {
        return Ok(input);
    }

    if input.parse::<IpAddr>().is_ok() {
        return Ok(input);
    }

    let url = match Url::parse(&input) {
        Ok(u) => u,
        Err(e) => return Err(format!("Invalid URL: {}", e)),
    };
    match url.host_str() {
        Some(ip) => Ok(ip.to_string()),
        None => Err(format!("Invalid IP address. {}", url)),
    }
}

fn set_upstream(args: &mut Vec<String>) -> Result<(), String> {
    assert!(args.len() >= 2);
    let ip = is_valid_ip(args.remove(0))?;

    let port = args.remove(0);
    let port = match port.parse::<u16>() {
        Ok(port) => port.to_string(),
        Err(e) => return Err(format!("Invalid port number. {}, {}", port, e)),
    };

    let ip = format!("{ip}:{port}");
    MerkleTree::set_upstream(".".into(), ip.to_string())?;

    Ok(())
}

fn status() -> Result<(), String> {
    let path = env::current_dir().unwrap();
    let branch_file = match MerkleTree::get_head_path(path.clone()) {
        Ok(t) => t,
        Err(e) => return Err(e.to_string()),
    };
    let hash_string =
        MerkleTree::get_branch_hash(path).expect("Unable to get hash for the current branch");

    println!("Branch:{}\nHash:{}", branch_file.display(), hash_string);
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
