use merkle::merklenode::traits::TreeIO;
use merkle::merkletree::MerkleTree;
use std::env;

fn display_help() {
    let strs = vec![
        "FreeSync:",
        "-h | --help) Prints the help menu",
        "-b | --blob [blob hash]) Decrypts blob and displays its contents",
        "--status) Prints the current status of the tree",
        "--build) Builds the tree",
        "--pull) Pulls the updates to the server",
        "--push Pushes the diffs to the server",
        "--branch [-n|-s]) Branch command:\n\t-n [name of branch]) Creates a new branch\n\t-s [name of branch]) Switches to another branch",
    ];

    for str in strs {
        println!("{}", str);
    }
}

fn execute_commands(mut args: Vec<String>) -> Vec<String> {
    debug_assert!(!args.is_empty());
    let opt = args.remove(0);

    match opt.as_str() {
        "-h" | "--help" => display_help(),
        "-b" | "--blob" => {
            debug_assert!(!args.is_empty());
            let path = args.remove(0);
            let node = MerkleTree::get_blob_data(&path);
            match node {
                Ok(node) => println!("{}", node),
                Err(msg) => println!("{}", msg),
            }
        }
        "--build" => {
            if let Err(e) = build_tree() {
                eprintln!("{}", e);
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
