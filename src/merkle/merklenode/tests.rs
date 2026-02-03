use crate::merkle::merklenode::leaf::LeafNode;
use crate::merkle::merklenode::node::Node;
use crate::merkle::merklenode::traits::{LeafData, LeafIO, TreeIO};
use crate::merkle::merkletree::MerkleTree;
use crate::merkle::traits::Hashable;
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn test_read_blob() {
    let temp_dir = TempDir::new().expect("Unable to create temporary directory");
    let temp_file = temp_dir.path().to_path_buf().join("blob");
    let str = crate::merkle::tests::write_random_to_filepath(&temp_file);

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

    match crate::merkle::tests::generate_random_tree(dir.path().to_path_buf()) {
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
