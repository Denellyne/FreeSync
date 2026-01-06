use sha2::{Digest, Sha256};
use std::fs;
use std::fs::DirEntry;

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq)]
pub struct TreeNode {
    pub(crate) hash: [u8; 32],
    children: Vec<Node>,
    file_path: String,
}
#[derive(Debug, Ord, PartialOrd, Eq, PartialEq)]
pub struct LeafNode {
    hash: [u8; 32],
    file_path: String,
}

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq)]
pub enum Node {
    Tree(TreeNode),
    Leaf(LeafNode),
}

impl Node {
    fn new_node(path: DirEntry) -> Option<Self> {
        match path.path() {
            path if path.is_dir() => Self::new_tree(path.display().to_string()),
            path if path.is_file() => Self::new_leaf(path.display().to_string()),
            _ => None,
        }
    }
    pub fn new_leaf(file_path: String) -> Option<Self> {
        let file_contents = fs::read(&file_path);
        match file_contents {
            Ok(contents) => {
                let new_hash = Sha256::digest(contents.as_slice());
                Some(Node::Leaf(LeafNode {
                    hash: new_hash.into(),
                    file_path,
                }))
            }
            Err(_) => {
                println!("Unable to read file {}", file_path);
                None
            }
        }
    }

    pub fn new_tree(dir_path: String) -> Option<Self> {
        let paths = fs::read_dir(&dir_path).expect("Unable to read directory");
        let mut vec: Vec<Node> = Vec::new();

        for path in paths {
            let path = path.expect("Unable to read directory entry");
            #[cfg(debug_assertions)]
            let path_for_debug = path.path();
            #[cfg(debug_assertions)]
            println!("Name: {}", &path_for_debug.display());

            match Self::new_node(path) {
                Some(node) => vec.push(node),
                None => {
                    #[cfg(debug_assertions)]
                    panic!(
                        "Path is not a file or directory: {}",
                        path_for_debug.display()
                    );
                    #[cfg(not(debug_assertions))]
                    panic!("Path is not a file or directory")
                }
            }
        }

        if vec.is_empty() {
            return None;
        }
        vec.sort_by(|a, b| a.get_path().cmp(b.get_path()));

        let mut data: Vec<u8> = Vec::with_capacity(vec.len() * 32);

        for index in 0..vec.len() {
            let children_hash = vec
                .get(index)
                .expect("Invalid access to children vector")
                .get_hash();
            data.extend_from_slice(&children_hash);
        }
        let new_hash = Sha256::digest(data);

        Some(Node::Tree(TreeNode {
            hash: new_hash.into(),
            children: vec,
            file_path: dir_path,
        }))
    }

    pub fn get_hash(&self) -> [u8; 32] {
        match self {
            Node::Tree(tree) => tree.hash,
            Node::Leaf(leaf) => leaf.hash,
        }
    }
    fn get_path(&self) -> &str {
        match self {
            Node::Tree(tree) => tree.file_path.as_str(),
            Node::Leaf(leaf) => leaf.file_path.as_str(),
        }
    }
}

#[cfg(test)]
mod tests {

    use crate::merkle::Node;
    use std::io::Write;
    use tempfile::NamedTempFile;

    #[test]
    fn test_new_leaf() {
        let mut temp_file = NamedTempFile::new().expect("Unable to create tempfile");
        let _ = write!(temp_file, "Hello World");
        let leaf = Node::new_leaf(
            temp_file
                .path()
                .to_str()
                .expect("Unable to find temp file path")
                .to_string(),
        );
        let decode_hex_str = |hex: &str| -> [u8; 32] {
            let mut vec: Vec<u8> = vec![];
            let convert_to_decimal = |ch: char| -> u8 {
                match ch {
                    '0'..='9' => ch as u8 - b'0',
                    'a'..='f' => ch as u8 - b'a' + 10,
                    'A'..='F' => ch as u8 - b'a' + 10,
                    _ => panic!("Char is out of bounds for hex base number"),
                }
            };

            for i in (0..hex.len()).step_by(2) {
                let fst: char = hex.chars().nth(i).expect("Character is null");
                let snd: char = hex.chars().nth(i + 1).expect("Character is null");
                let converted = convert_to_decimal(fst) << 4 | convert_to_decimal(snd);
                vec.push(converted);
            }
            let result: [u8; 32] = vec
                .as_slice()
                .try_into()
                .expect("Unable to convert Vec<u8> into [u8;32]");
            result
        };
        match leaf {
            Some(Node::Leaf(leaf)) => {
                let hex_str = "a591a6d40bf420404a011733cfb7b190d62c65bf0bcda32b57b277d9ad9f146e";
                let vec: [u8; 32] = decode_hex_str(hex_str);
                assert_eq!(leaf.hash, vec);
            }
            _ => panic!("Unable to create leaf node"),
        }
    }
}
