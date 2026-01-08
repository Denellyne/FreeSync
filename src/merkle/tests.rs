use crate::merkle::node::{Change, Diff};
use crate::merkle::traits::LeafData;
use crate::merkle::*;
use rand::random;
use std::io::Write;
use std::path;
use std::path::PathBuf;
use tempfile::{NamedTempFile, TempDir, tempdir_in, tempfile};

fn random_tree_builder(
    path: Option<PathBuf>,
) -> (Result<Node, String>, Vec<Diff>, Option<TempDir>) {
    match path {
        Some(path) => {
            let (node, vec) = generate_random_tree(path);
            (node, vec, None)
        }
        None => {
            let temp_dir = tempfile::tempdir().expect("Unable to create temp dir");
            let (node, vec) = generate_random_tree(temp_dir.path().to_path_buf());
            (node, vec, Some(temp_dir))
        }
    }
}

fn generate_file(contents: &str) -> NamedTempFile {
    let file = NamedTempFile::new().expect("Unable to create temporary file");
    write!(&file, "{}", contents).expect("Unable to write to file");
    file
}
fn write_random_to_file(file: NamedTempFile) -> (NamedTempFile,String) {
    let mut str: String = String::new();
    let len = random::<u16>() % u16::MAX + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    write!(&file, "{}", str).expect("Unable to write to file");
    (file,str)
}

fn generate_random_file(path: &PathBuf) -> NamedTempFile {


    let (file,_) = write_random_to_file(NamedTempFile::new_in(&path).expect("Unable to create temporary file"));
    file
}
fn generate_random_tree(path: PathBuf) -> (Result<Node, String>, Vec<Diff>) {
    let mut differences: Vec<Diff> = Vec::new();

    let size = random::<u8>() % u8::MAX + 1;
    let mut first: bool = true;
    let mut current_path: PathBuf = path.clone();
    let mut temporary_files: Vec<NamedTempFile> = Vec::new();
    let mut temporary_folders: Vec<TempDir> = Vec::new();

    let get_relative_path =
        |str: &path::Path| -> PathBuf { str.file_name().expect("Unable to get file name").into() };

    for _i in 0..size {
        let gen_dir = random::<bool>();
        if gen_dir {
            let temp_file = tempdir_in(&current_path).expect("Unable to create temporary folder");
            let relative_path = get_relative_path(&temp_file.path());
            current_path.push(&relative_path);

            if first {
                differences.push(Diff::Created {
                    file_path: current_path.clone(),
                });
                first = false;
            }

            temporary_folders.push(temp_file);
        } else {
            let temp_file = generate_random_file(&current_path);
            let relative_path: PathBuf = current_path.join(&get_relative_path(&temp_file.path()));
            temporary_files.push(temp_file);
            if first {
                differences.push(Diff::Created {
                    file_path: relative_path,
                });
            }
        }
    }

    let tree = Node::Tree(MerkleBuilder::new(path.to_path_buf()).expect("Unable to create tree"));
    (Ok(tree), differences)
}

#[test]
fn test_new_tree() {
    let (t1, _, _) = random_tree_builder(None::<PathBuf>);
    assert!(t1.is_ok());
}
#[test]
fn test_trees_are_different() {
    let (t1, _, temp_folder) = random_tree_builder(None::<PathBuf>);
    let (t2, differences, _) = random_tree_builder(Some(
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
    match t1.find_differences(&t2) {
        Some(contents) => assert_ne!(differences, contents),
        _ => panic!("Unable to find differences for tree"),
    }
}
#[test]
fn test_new_leaf() {
    let temp_file = generate_random_file(&PathBuf::from("."));
    let leaf = MerkleBuilder::new_leaf(temp_file.path().to_path_buf());
    assert!(leaf.is_ok());
}

#[test]
fn test_diff() {
    let f1 = generate_file("abcdfghjqz");
    let f2 = generate_file("abcdefgijkrxyz");
    let leaf1 = MerkleBuilder::new_leaf(f1.path().to_path_buf()).expect("Unable to create leaf 1");
    let leaf2 = MerkleBuilder::new_leaf(f2.path().to_path_buf()).expect("Unable to create leaf 2");

    let diff2 = vec![
        Change::Copy { start: 0, end: 3 },
        Change::Insert {
            data: [120, 218, 75, 5, 0, 0, 102, 0, 102].to_vec(),
        },
        Change::Copy { start: 4, end: 5 },
        Change::Delete { start: 6, end: 6 },
        Change::Insert {
            data: [120, 218, 203, 4, 0, 0, 106, 0, 106].to_vec(),
        },
        Change::Copy { start: 7, end: 7 },
        Change::Delete { start: 8, end: 8 },
        Change::Insert {
            data: [120, 218, 203, 46, 170, 168, 4, 0, 4, 111, 1, 207].to_vec(),
        },
        Change::Copy { start: 9, end: 9 },
        Change::End,
    ];

    match (leaf1, leaf2) {
        (Node::Leaf(leaf1), Node::Leaf(leaf2)) => {
            let diff1 = leaf1.diff_file(&leaf2);
            assert_eq!(diff1, diff2);
        }
        _ => panic!("Unable to create diff"),
    }


}

#[test]
fn test_compression(){
    use super::*;

    let temp_file : NamedTempFile = NamedTempFile::new().expect("Unable to create temporary file");
    let (temp_file,str) = write_random_to_file(temp_file);
    let leaf = MerkleBuilder::new_leaf(temp_file.path().to_path_buf());
    match leaf {
        Ok(leaf) => {
            match leaf {
                Node::Leaf(leaf) => {
                    let decompress = LeafNode::decompress(&leaf.compressed_data);

                    assert_eq!(decompress,str.as_bytes());
                }
                _ => panic!("Unable to create leaf"),
            }
        },
        Err(_) => panic!("Unable to create leaf"),
    }

}
