pub(crate) use crate::merkle::diff::{Change, Diff};
use crate::merkle::traits::LeafData;
use std::collections;
use std::collections::BTreeMap;
use std::hash::Hash;
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

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub enum Node {
    Tree(TreeNode),
    Leaf(LeafNode),
}
// Todo create a builder for a merkle tree and make the data structures pure

impl Node {
    pub fn get_hash_string(hash: [u8; 32]) -> String {
        let mut str = String::new();

        for ch in hash {
            str += ch.to_string().as_str();
        }

        str
    }
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

    pub fn find_differences(&self, other: &Node) -> Option<Vec<Diff>> {
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
