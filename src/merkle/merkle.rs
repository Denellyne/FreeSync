use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::fs;
use std::fs::DirEntry;

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub struct TreeNode {
    pub(crate) hash: [u8; 32],
    children: Vec<Node>,
    file_path: String,
}
#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub struct LeafNode {
    hash: [u8; 32],
    file_path: String,
}

#[derive(Debug, Ord, PartialOrd, Eq, PartialEq, Hash)]
pub enum Node {
    Tree(TreeNode),
    Leaf(LeafNode),
}

impl Node {
    fn new_node(path: DirEntry) -> Option<Self> {
        match path.path() {
            path if path.is_dir() => Self::new_tree(path.display().to_string()),
            path if path.is_file() => Self::new_leaf(path.display().to_string()),
            path if path.is_symlink() => {
                eprintln!("Path is a symlink: {}", path.display());
                None
            }
            _ => {
                panic!("Path is not a file or directory: {}", path.path().display());
            }
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
                None => {}
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

    fn find_differences(&self, other: &Node) -> Option<Vec<String>> {
        if self.get_hash() == other.get_hash() {
            return None;
        }
        let mut differences: Vec<String> = Vec::new();

        match (self, other) {
            (Node::Tree(tree1), Node::Tree(tree2)) => {
                let hashset1: HashSet<_> = HashSet::from_iter(tree1.children.iter());
                let hashset2: HashSet<_> = HashSet::from_iter(tree2.children.iter());
                let disjoin: Vec<&Node> =
                    hashset1.symmetric_difference(&hashset2).copied().collect();
                for node in disjoin {
                    differences.push(node.get_path().to_string());
                }
                let mut v1: Vec<&Node> = hashset1.intersection(&hashset2).copied().collect();
                let mut v2: Vec<&Node> = hashset2.intersection(&hashset1).copied().collect();
                v1.sort_by(|a, b| a.get_path().cmp(b.get_path()));
                v2.sort_by(|a, b| a.get_path().cmp(b.get_path()));

                for (c1, c2) in v1.iter().zip(v2.iter()) {
                    match c1.find_differences(c2) {
                        Some(vec) => {
                            differences.extend_from_slice(&vec);
                        }
                        None => {}
                    }
                }
                Some(differences)
            }
            (Node::Leaf(_), Node::Leaf(leaf2)) => Some([leaf2.file_path.clone()].to_vec()),
            _ => panic!("Nodes weren't of the same type"),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::merkle::Node;
    use rand::random;
    use std::collections::HashSet;
    use std::io::Write;
    use std::path;
    use std::path::PathBuf;
    use tempfile::{NamedTempFile, TempDir, tempdir_in};

    fn generate_random_tree() -> (Node, Vec<String>) {
        let mut differences: Vec<String> = Vec::new();

        let size: u8 = random::<u8>() % 255 + 1;
        let mut first: bool = true;
        let mut current_path: PathBuf = PathBuf::from(".");
        let mut temporary_files: Vec<NamedTempFile> = Vec::new();
        let mut temporary_folders: Vec<TempDir> = Vec::new();

        let write_random_to_file = |file: NamedTempFile| {
            let mut str: String = String::new();
            let len = random::<u8>() + 1;
            for _i in 0..len {
                str.push(random::<char>());
            }
            write!(&file, "{}", str).expect("Unable to write to file");
            file
        };

        let get_relative_path = |str: &path::Path| -> String {
            str.file_name()
                .expect("Unable to get file name")
                .to_str()
                .expect("Unable to convert to str")
                .to_string()
        };

        for _i in 0..size {
            //println!("current_path: {}", current_path.display());
            let gen_dir = random::<bool>();
            if gen_dir {
                let temp_file =
                    tempdir_in(&current_path).expect("Unable to create temporary folder");
                //   println!("Temporary folder created at {}", temp_file.path().display());
                let relative_path = get_relative_path(&temp_file.path());
                current_path.push(&relative_path);

                if first {
                    differences.push(
                        current_path
                            .to_str()
                            .expect("Unable to convert current_path to str")
                            .to_string(),
                    );
                    first = false;
                }

                temporary_folders.push(temp_file);
            } else {
                let temp_file =
                    NamedTempFile::new_in(&current_path).expect("Unable to create temporary file");
                //   println!("temp_file: {}", temp_file.path().display());
                let relative_path = current_path
                    .join(&get_relative_path(&temp_file.path()))
                    .to_str()
                    .expect("Unable to create relative path string")
                    .to_string();
                //  println!("Relative path: {}", &relative_path);
                let temp_file = write_random_to_file(temp_file);
                temporary_files.push(temp_file);
                if first {
                    differences.push(relative_path);
                }
            }
        }
        (
            Node::new_tree(String::from(".")).expect("Unable to generate random tree"),
            differences,
        )
    }

    #[test]
    fn test_new_tree() {
        let t1 = Node::new_tree(String::from("."));
        assert!(t1.is_some());
    }
    #[test]
    fn test_trees_are_different() {
        let t1 = Node::new_tree(String::from(".")).expect("Unable to create tree t1");
        let (t2, differences) = generate_random_tree();

        assert_ne!(&t1, &t2);
        let hashset: HashSet<_> = HashSet::from_iter(differences.iter());
        assert!(
            t1.find_differences(&t2)
                .expect("There should be atleast 1 difference")
                .iter()
                .all(|x| hashset.contains(x))
        )
    }
    #[test]
    fn test_new_leaf() {
        let temp_file = NamedTempFile::new().expect("Unable to create temporary file");
        write!(&temp_file, "Hello World").expect("Unable to write to file");

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
