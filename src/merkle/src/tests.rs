use rand::random;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use tempfile::{NamedTempFile, TempDir, tempdir_in};
use crate::merklenode::leaf::LeafNode;
use crate::merklenode::node::Node;
use crate::merklenode::traits::LeafData;
use crate::merkletree::MerkleTree;

pub(crate) fn random_tree_builder(
    path: Option<PathBuf>,
) -> (Result<Node, String>, Option<TempDir>) {
    match path {
        Some(path) => {
            let (node, _, _) = generate_random_tree(path);
            (node, None)
        }
        None => {
            let temp_dir = tempfile::tempdir().expect("Unable to create temp dir");
            let (node, _, _) = generate_random_tree(temp_dir.path().to_path_buf());
            (node, Some(temp_dir))
        }
    }
}

pub(crate) fn generate_file(contents: &str, path: &Path) -> NamedTempFile {
    let file = NamedTempFile::new_in(path).expect("Unable to create temporary file");
    write!(&file, "{}", contents).expect("Unable to write to file");
    file
}

pub(crate) fn write_random_to_filepath(path: &PathBuf) -> String {
    let file: File = OpenOptions::new()
        .create(true)
        .truncate(false)
        .write(true)
        .open(path)
        .unwrap_or_else(|_| panic!("Unable to open file {}", path.display()));

    let mut str: String = String::new();
    let len = random::<u8>() % u8::MAX + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    write!(&file, "{}", str).expect("Unable to write to file");
    str
}
fn write_random_to_file(file: NamedTempFile) -> (NamedTempFile, String) {
    let mut str: String = String::new();
    let len = random::<u16>() % u16::MAX / 4 + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    write!(&file, "{}", str).expect("Unable to write to file");
    (file, str)
}

fn generate_random_file(path: &PathBuf) -> NamedTempFile {
    let (file, _) =
        write_random_to_file(NamedTempFile::new_in(path).expect("Unable to create temporary file"));
    file
}
pub(crate) fn generate_random_tree(
    path: PathBuf,
) -> (Result<Node, String>, Vec<NamedTempFile>, Vec<TempDir>) {
    let size = random::<u8>() % 12 + 1;
    let mut current_path: PathBuf = path.clone();
    let mut temporary_files: Vec<NamedTempFile> = Vec::new();
    let mut temporary_folders: Vec<TempDir> = Vec::new();

    let get_relative_path =
        |str: &Path| -> PathBuf { str.file_name().expect("Unable to get file name").into() };

    for _i in 0..size {
        let gen_dir = random::<bool>();
        if gen_dir {
            let temp_file = tempdir_in(&current_path).expect("Unable to create temporary folder");
            let relative_path = get_relative_path(temp_file.path());
            current_path.push(&relative_path);

            temporary_folders.push(temp_file);
        } else {
            let temp_file = generate_random_file(&current_path);
            temporary_files.push(temp_file);
        }
    }

    let tree = Node::Tree(MerkleTree::create(path.to_path_buf()).expect("Unable to create tree"));
    (Ok(tree), temporary_files, temporary_folders)
}

#[test]
fn test_new_tree() {
    let (t1, _) = random_tree_builder(None::<PathBuf>);
    assert!(t1.is_ok());
}
#[test]
fn test_trees_are_different() {
    let (t1, temp_folder) = random_tree_builder(None::<PathBuf>);
    let (t2, _) = random_tree_builder(Some(
        temp_folder
            .expect("Expected path from temp folder")
            .path()
            .to_path_buf(),
    ));

    let t1 = match t1 {
        Ok(tree) => tree,
        Err(e) => panic!("Unable to create MerkleBuilder: {}", e),
    };
    let t2 = match t2 {
        Ok(tree) => tree,
        Err(e) => panic!("Unable to create MerkleBuilder: {}", e),
    };

    assert_ne!(&t1, &t2);
    match t1.find_differences(t2) {
        Ok(contents) => match contents {
            Some(contents) => assert!(!contents.is_empty()),
            None => panic!("Unable to find differences"),
        },
        _ => panic!("Unable to find differences for tree"),
    }
}

#[test]
fn test_new_leaf() {
    let temp_file = generate_random_file(&PathBuf::from("../../.."));
    let leaf = MerkleTree::new_leaf(temp_file.path().to_path_buf());
    assert!(leaf.is_ok());
}

#[test]
fn test_compression() {
    let temp_file: NamedTempFile = NamedTempFile::new().expect("Unable to create temporary file");
    let (temp_file, str) = write_random_to_file(temp_file);
    let leaf = MerkleTree::new_leaf(temp_file.path().to_path_buf());
    match leaf {
        Ok(leaf) => {
            let decompress =
                LeafNode::decompress_data(&leaf.compressed_data).expect("Unable to decompress");
            assert_eq!(decompress, str.as_bytes());
        }
        Err(_) => panic!("Unable to create leaf"),
    }
}
