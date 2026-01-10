use crate::merkle::diff::diff::Change;
use crate::merkle::merklenode::traits::LeafData;
use crate::merkle::merkletree::MerkleTree;

#[test]
fn test_diff() {
    let f1 = crate::merkle::tests::generate_file("abcdfghjqz");
    let f2 = crate::merkle::tests::generate_file("abcdefgijkrxyz");
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
        Change::End,
    ];

    let diff1 = leaf1.diff_file(&leaf2);
    assert_eq!(diff1, diff2);
}
