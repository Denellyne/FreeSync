use crate::merklenode::diff::Change;
use crate::merklenode::leaf::LeafNode;
use crate::merklenode::node::Node;
use crate::merklenode::node::Node::Tree;
use crate::merklenode::traits::{LeafData, LeafIO, TreeIO};
use crate::merkletree::MerkleTree;
use crate::tests::{generate_file, random_tree_builder};
use crate::tests::{generate_random_tree, write_random_to_filepath};
use crate::traits::Hashable;
use std::fs::write;
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn test_read_blob() {
    let temp_dir = TempDir::new().expect("Unable to create temporary directory");
    let temp_file = temp_dir.path().to_path_buf().join("blob");
    let str = write_random_to_filepath(&temp_file);

    let leaf1 =
        MerkleTree::new_leaf(temp_file.as_path().to_path_buf()).expect("Unable to create tree");
    leaf1
        .write_blob(temp_dir.as_ref())
        .expect("Unable to write data");
    let blob_path = temp_dir
        .path()
        .join(&LeafNode::hash_to_hex_string(&leaf1.hash)[..2])
        .join(&LeafNode::hash_to_hex_string(&leaf1.hash)[2..]);

    let leaf2 = LeafNode::from(&blob_path, blob_path.clone()).expect("Unable to create tree");

    let leaf2str = String::from_utf8(
        LeafNode::decompress_data(leaf2.data()).expect("Unable to decompress data"),
    )
    .expect("Unable to decode tree data");

    assert_eq!(leaf1.hash, leaf2.hash);
    assert_eq!(str, leaf2str);
    assert_eq!(leaf1.data(), leaf2.data());
}

#[test]
fn test_read_write_tree() {
    let dir: TempDir = TempDir::new().expect("Unable to create temporary folder");
    let path = PathBuf::from(dir.path());

    match generate_random_tree(dir.path().to_path_buf()) {
        (Ok(Node::Tree(tree)), _files, _dirs) => match tree.save_tree() {
            Ok(_) => {
                let t2 =
                    MerkleTree::from(&path, path.clone()).expect("Unable to create second tree");
                assert_eq!(Node::Tree(tree), t2);
            }
            Err(e) => panic!("{}", e),
        },
        (Ok(Node::Leaf(_)), _, _) => panic!("Returned a leaf from random tree function"),
        (Err(e), _, _) => panic!("Unable to create MerkleTree: {}", e),
    };
}

#[test]
fn test_diff() {
    let dir = TempDir::new().unwrap();
    let dir_path = dir.path();
    let f1 = generate_file("abcdfghjqz", dir_path);
    let f2 = generate_file("abcdefgijkrxyz", dir_path);
    let leaf1 = MerkleTree::new_leaf(f1.path().to_path_buf()).expect("Unable to create leaf 1");
    let leaf2 = MerkleTree::new_leaf(f2.path().to_path_buf()).expect("Unable to create leaf 2");

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
        Change::End {
            final_hash: leaf2.hash,
        },
    ];

    let diff1 = match leaf1.diff_file(&leaf2) {
        Ok(diff) => diff,
        Err(e) => panic!("{}", e),
    };

    assert_eq!(diff1, diff2);
}

#[test]
fn tree_from_diff_simple() {
    let dir = TempDir::new().unwrap();
    let dir_path = dir.path();
    let f1 = generate_file("abcdfghjqz", dir_path);

    let t1 = Tree(MerkleTree::create(dir_path.to_path_buf()).expect("Unable to create tree 1"));

    write(&f1, "abcdefgijkrxyz").expect("Unable to write to file");
    let t2 = Tree(MerkleTree::create(dir_path.to_path_buf()).expect("Unable to create tree 2"));
    assert_ne!(&t1, &t2);

    let t1: Node = match t1.clone().find_differences(t2.clone()) {
        Ok(contents) => match contents {
            None => {
                panic!("No differences found");
            }
            Some(contents) => match t1 {
                Tree(t1) => {
                    let mut t3 = t1.clone();

                    t3.apply_diff(contents).expect("Unable to apply diff");

                    Tree(t3)
                }
                _ => panic!("Unable to apply diff"),
            },
        },

        Err(e) => panic!("{}", e),
    };

    assert_eq!(t1, t2)
}
#[test]
fn tree_from_diff() {
    let (t1, temp_folder) = random_tree_builder(None::<PathBuf>);
    let (t2, _) = random_tree_builder(Some(
        temp_folder
            .expect("Expected path from temp folder")
            .path()
            .to_path_buf(),
    ));

    let mut t1 = match t1 {
        Ok(tree) => tree,
        Err(e) => panic!("Unable to create MerkleBuilder: {}", e),
    };
    let t2 = match t2 {
        Ok(tree) => tree,
        Err(e) => panic!("Unable to create MerkleBuilder: {}", e),
    };

    assert_ne!(&t1, &t2);
    match t1.find_differences(t2.clone()) {
        Ok(contents) => match contents {
            Some(contents) => {
                t1.apply_diff(contents).expect("Unable to apply diff");
                assert_eq!(t1, t2)
            }
            None => panic!("Unable to find differences"),
        },
        _ => panic!("Unable to find differences for tree"),
    }
}
