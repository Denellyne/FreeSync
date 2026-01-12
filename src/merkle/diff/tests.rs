use crate::merkle::diff::diff::Change;
use crate::merkle::merklenode::node::Node;
use crate::merkle::merklenode::node::Node::Tree;
use crate::merkle::merklenode::traits::LeafData;
use crate::merkle::merkletree::MerkleTree;
use std::fs::write;
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn test_diff() {
    let dir = TempDir::new().unwrap();
    let dir_path = dir.path();
    let f1 = crate::merkle::tests::generate_file("abcdfghjqz", dir_path);
    let f2 = crate::merkle::tests::generate_file("abcdefgijkrxyz", dir_path);
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
    let f1 = crate::merkle::tests::generate_file("abcdfghjqz", dir_path);

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
    let (t1, temp_folder) = crate::merkle::tests::random_tree_builder(None::<PathBuf>);
    let (t2, _) = crate::merkle::tests::random_tree_builder(Some(
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
    match t1.find_differences(t2.clone()) {
        Ok(contents) => match contents {
            Some(contents) => {
                t1.to_owned()
                    .apply_diff(contents)
                    .expect("Unable to apply diff");
// Adicionar novos diretorios ta bugado!!
               assert_eq!(t1, t2)
            }
            None => core::panic!("Unable to find differences"),
        },
        _ => core::panic!("Unable to find differences for tree"),
    }
}
