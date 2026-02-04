use std::collections::BTreeMap;
use std::fs::DirEntry;
use std::path::{Path, PathBuf};
use std::{collections, fs};
use crate::diff::diff::Diff;
use crate::merklenode::leaf::LeafNode;
use crate::merklenode::traits::LeafData;
use crate::merklenode::tree::TreeNode;
use crate::traits::Hashable;

#[derive(Debug, Eq, PartialEq, Clone, Hash)]
pub enum Node {
    Tree(TreeNode),
    Leaf(LeafNode),
}
impl Hashable for Node {
    fn hash(vec: &[u8]) -> [u8; 32] {
        <LeafNode as Hashable>::hash(vec)
    }

    fn get_hash(&self) -> [u8; 32] {
        match self {
            Node::Tree(tree) => tree.get_hash(),
            Node::Leaf(leaf) => leaf.get_hash(),
        }
    }
}
impl Node {
    pub fn new(path: DirEntry) -> Result<Node, String> {
        match path.path() {
            path if path.is_dir() => Ok(Node::Tree(TreeNode::new(path)?)),
            path if path.is_file() => Ok(Node::Leaf(LeafNode::new(path)?)),
            _ => Err(format!(
                "Unable to generate new node, {}",
                path.path().display()
            )),
        }
    }

    pub fn from(path: impl AsRef<Path>, real_path: PathBuf) -> Result<Node, String> {
        let path = path.as_ref();
        let metadata = match fs::metadata(path) {
            Ok(metadata) => metadata,
            Err(e) => return Err(e.to_string()),
        };

        match metadata {
            metadata if metadata.is_dir() => Ok(Node::Tree(TreeNode::from(path, real_path)?)),
            metadata if metadata.is_file() => Ok(Node::Leaf(LeafNode::from(path, real_path)?)),
            _ => Err(format!("Unable to generate new node, {}", path.display())),
        }
    }

    pub fn apply_diff(&mut self, diffs: Vec<Diff>) -> Result<(), String> {
        if diffs.is_empty() {
            return Ok(());
        }

        match self {
            Node::Tree(tree) => tree.apply_diff(diffs),
            _ => Err("Apply diff is invalid for leaf nodes".to_string()),
        }
    }

    pub fn get_path(&self) -> &PathBuf {
        match self {
            Node::Tree(tree) => &tree.file_path,
            Node::Leaf(leaf) => &leaf.file_path,
        }
    }

    pub fn get_filename(&self) -> Result<&str, String> {
        match self.get_path().file_name() {
            Some(file_name) => match file_name.to_str() {
                Some(file_name) => Ok(file_name),
                None => Err("File name is not valid UTF-8".to_string()),
            },
            None => Err(String::from("File name not set")),
        }
    }

    fn separate_different<'a, 'b>(
        tree1: &'a TreeNode,
        tree2: &'b TreeNode,
    ) -> Option<(Vec<&'a Node>, Vec<&'b Node>, Vec<Diff>)> {
        let mut differences: Vec<Diff> = Vec::new();
        let map1: BTreeMap<&'a PathBuf, &'a Node> =
            tree1.children.iter().map(|n| (n.get_path(), n)).collect();

        let map2: BTreeMap<&'b PathBuf, &'b Node> =
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
                    file_path: path.to_path_buf(),
                }),

                (None, Some(node)) => differences.push(Diff::Created {
                    node: (*node).clone(),
                }),

                _ => return None,
            }
        }
        common1.sort_by(|a, b| a.get_path().cmp(b.get_path()));
        common2.sort_by(|a, b| a.get_path().cmp(b.get_path()));
        Some((common1, common2, differences))
    }

    pub fn find_differences(&self, other: Node) -> Result<Option<Vec<Diff>>, String> {
        if self.get_hash() == other.get_hash() {
            return Ok(None);
        }

        match (self, other) {
            (Node::Tree(tree1), Node::Tree(tree2)) => {
                let (common1, common2, mut differences) =
                    match Self::separate_different(tree1, &tree2) {
                        Some((common1, common2, differences)) => (common1, common2, differences),
                        None => (
                            tree1.children.iter().collect(),
                            tree2.children.iter().collect(),
                            vec![],
                        ),
                    };

                for (c1, c2) in common1.iter().zip(common2.iter()) {
                    match c1.find_differences((*c2).clone()) {
                        Ok(vec) => {
                            if let Some(diff) = vec {
                                differences.extend(diff)
                            }
                        }

                        Err(msg) => return Err(msg),
                    };
                }
                Ok(Some(differences))
            }
            (Node::Leaf(leaf1), Node::Leaf(leaf2)) => {
                let file_changed = Diff::Changed {
                    file_path: leaf2.file_path.clone().to_path_buf(),
                    changes: match leaf1.diff_file(&leaf2) {
                        Ok(changes) => changes,
                        Err(e) => return Err(e.to_string()),
                    },
                };
                Ok(Some(vec![file_changed]))
            }
            _ => Ok(None),
        }
    }
}
