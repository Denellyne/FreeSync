use crate::merkle::traits::{CompressedData, LeafData};
use std::collections;
use std::collections::BTreeMap;
use std::path::PathBuf;

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub(crate) struct TreeNode {
    pub(crate) hash: [u8; 32],
    pub(crate) children: Vec<Node>,
    pub(crate) file_path: PathBuf,
}
#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub(crate) struct LeafNode {
    pub(crate) hash: [u8; 32],
    pub(crate) compressed_data: Vec<u8>,
    pub(crate) file_path: PathBuf,
}

impl LeafData for LeafNode {
    fn data(&self) -> &Vec<u8> {
        &self.compressed_data
    }

    fn diff_file(&self, other: &Self) -> Vec<Change> {
        fn diff<'a>(
            v1: &'a [u8],
            v2: &'a [u8],
            v1_start: u64,
            v2_start: u64,
        ) -> (Change, &'a [u8], &'a [u8], u64, u64) {
            fn should_delete(v1: &[u8], v2: &[u8]) -> bool {
                const LOOK: usize = 32;

                let d = v1.iter().take(LOOK).position(|&x| x == v2[0]);
                let i = v2.iter().take(LOOK).position(|&x| x == v1[0]);

                match (d, i) {
                    (Some(d), Some(i)) => d <= i,
                    (Some(_), None) => true,
                    (None, Some(_)) => false,
                    _ => true,
                }
            }

            if v1.is_empty() && !v2.is_empty() {
                return (
                    Change::Insert { data: LeafNode::compress(&v2.to_vec()) },
                    &[],
                    &[],
                    v1_start,
                    v2_start + v2.len() as u64,
                );
            }
            if v2.is_empty() && !v1.is_empty() {
                return (
                    Change::Delete {
                        start: v1_start,
                        end: v1.len() as u64 - v1_start-1,
                    },
                    &[],
                    &[],
                    v1_start + v1.len() as u64,
                    v2_start,
                );
            }
            if v1.is_empty() && v2.is_empty() {
                return (Change::End, &[], &[], v1_start, v2_start);
            }

            let mut len = 0;
            while len < v1.len() && len < v2.len() && v1[len] == v2[len] {
                len += 1;
            }
            if len > 0 {
                return (
                    Change::Copy {
                        start: v1_start,
                        end: v1_start + len as u64 -1,
                    },
                    &v1[len..],
                    &v2[len..],
                    v1_start + len as u64,
                    v2_start + len as u64,
                );
            }

            if should_delete(v1, v2) {
                let delete_len = 1;
                (
                    Change::Delete {
                        start: v1_start,
                        end: v1_start + delete_len-1,
                    },
                    &v1[delete_len as usize..],
                    v2,
                    v1_start + delete_len,
                    v2_start,
                )
            } else {
                let mut insert_len = 1;
                while insert_len < v2.len() && v1[0] != v2[insert_len] {
                    insert_len += 1;
                }
                (
                    Change::Insert {
                        data: LeafNode::compress(&v2[..insert_len].to_vec()),
                    },
                    v1,
                    &v2[insert_len..],
                    v1_start,
                    v2_start + insert_len as u64,
                )
            }
        }

        let v1 = LeafNode::decompress(&self.compressed_data);
        let v2 = LeafNode::decompress(&other.compressed_data);
        let mut leaf1_data = v1.as_slice();
        let mut leaf2_data = v2.as_slice();

        let mut changes: Vec<Change> = Vec::new();

        let mut start_leaf1: u64 = 0;
        let mut start_leaf2: u64 = 0;
        loop {
            let change: Change;
            (change, leaf1_data, leaf2_data, start_leaf1, start_leaf2) =
                diff(leaf1_data, leaf2_data, start_leaf1, start_leaf2);
            match change {
                Change::End => {
                    changes.push(change);
                    return changes;
                }
                _ => changes.push(change),
            }
        }
    }
}

impl CompressedData for LeafNode {}

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub enum Node {
    Tree(TreeNode),
    Leaf(LeafNode),
}
// Todo create a builder for a merkle tree and make the data structures pure

#[derive(PartialEq, Eq, Debug, Ord, PartialOrd)]
pub(crate) enum Diff {
    Created {
        file_path: PathBuf,
    },
    Deleted {
        file_path: PathBuf,
    },
    Changed {
        file_path: PathBuf,
        changes: Vec<Change>,
    },
}
#[derive(PartialEq, Eq, Debug, Ord, PartialOrd)]
pub(crate) enum Change {
    Copy { start: u64, end: u64 },
    Delete { start: u64, end: u64 },
    Insert { data: Vec<u8> },
    End,
}

impl Node {
    pub fn get_hash(&self) -> [u8; 32] {
        match self {
            Node::Tree(tree) => tree.hash,
            Node::Leaf(leaf) => leaf.hash,
        }
    }
    pub fn get_path(&self) -> &PathBuf {
        match self {
            Node::Tree(tree) => &tree.file_path,
            Node::Leaf(leaf) => &leaf.file_path,
        }
    }

    fn separate_different<'a, 'b>(
        tree1: &'a TreeNode,
        tree2: &'b TreeNode,
    ) -> (Vec<&'a Node>, Vec<&'b Node>, Vec<Diff>) {
        let mut differences = Vec::new();
        let map1: BTreeMap<&PathBuf, &Node> =
            tree1.children.iter().map(|n| (n.get_path(), n)).collect();

        let map2: BTreeMap<&PathBuf, &Node> =
            tree2.children.iter().map(|n| (n.get_path(), n)).collect();
        let mut common1: Vec<&Node> = Vec::new();
        let mut common2: Vec<&Node> = Vec::new();

        for path in map1
            .keys()
            .chain(map2.keys())
            .collect::<collections::BTreeSet<_>>()
        {
            match (map1.get(path), map2.get(path)) {
                (Some(n1), Some(n2)) => {
                    common1.push(n1);
                    common2.push(n2);
                }
                (Some(_), None) => differences.push(Diff::Deleted {
                    file_path: (*path).clone(),
                }),

                (None, Some(_)) => differences.push(Diff::Created {
                    file_path: (*path).clone(),
                }),

                _ => panic!("Unable to find differences for path"),
            }
        }
        common1.sort_by(|a, b| a.get_path().cmp(b.get_path()));
        common2.sort_by(|a, b| a.get_path().cmp(b.get_path()));
        (common1, common2, differences)
    }

    pub(crate) fn find_differences(&self, other: &Node) -> Option<Vec<Diff>> {
        if self.get_hash() == other.get_hash() {
            return None;
        }

        match (self, other) {
            (Node::Tree(tree1), Node::Tree(tree2)) => {
                let (common1, common2, mut differences) = Self::separate_different(&tree1, &tree2);

                for (c1, c2) in common1.iter().zip(common2.iter()) {
                    match c1.find_differences(c2) {
                        Some(vec) => {
                            differences.extend(vec);
                        }
                        None => {}
                    }
                }
                Some(differences)
            }
            (Node::Leaf(leaf1), Node::Leaf(leaf2)) => {
                let file_changed = Diff::Changed {
                    file_path: leaf2.file_path.clone(),
                    changes: leaf1.diff_file(leaf2),
                };
                Some(vec![file_changed])
            }
            _ => panic!("Nodes weren't of the same type"),
        }
    }
}
