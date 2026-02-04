use crate::diff::diff::{Change, Diff};
use crate::merklenode::leaf::LeafNode;
use crate::merklenode::node::Node;
use crate::merklenode::node::Node::{Leaf, Tree};
use crate::merklenode::traits::internal_traits::TreeIOInternal;
use crate::merklenode::traits::{EntryData, HashableNode, Header, LeafIO, TreeIO};
use crate::traits::{Hashable, ReadFile};
use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Eq, PartialEq, Clone, Hash)]
pub struct TreeNode {
    pub(crate) hash: [u8; 32],
    pub(crate) children: Vec<Node>,
    pub(crate) file_path: PathBuf,
}

impl std::fmt::Debug for TreeNode {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.debug_struct("TreeNode")
            .field("Folder", &self.file_path)
            .field("Children", &self.children)
            .finish()
    }
}

impl TreeNode {
    pub(crate) fn new(path: impl AsRef<Path>) -> Result<TreeNode, String> {
        assert!(
            path.as_ref().exists(),
            "There isn't any folder in the path {}",
            path.as_ref().display()
        );

        let dir_path = path.as_ref();
        let paths = Self::read_dir(dir_path);
        let paths = paths?;
        let mut vec: Vec<Node> = Vec::new();

        let filter: HashSet<_> = HashSet::from([".freesync"]);
        'pathLoop: for path in paths {
            let path = match path {
                Ok(path) => path,
                Err(_) => return Err(format!("Unable to read directory entry, path: {:?}", path)),
            };

            for str in filter.iter().collect::<Vec<_>>() {
                match path.file_name().to_str() {
                    Some(file_name) => {
                        if file_name.contains(str) {
                            continue 'pathLoop;
                        }
                    }
                    None => return Err(format!("Unable to read file name: {:?}", &path)),
                };
            }

            match Node::new(path) {
                Ok(node) => vec.push(node),
                Err(e) => return Err(format!("{} at {}", e, dir_path.display())),
            }
        }

        Ok(TreeNode {
            hash: TreeNode::hash_tree(&mut vec),
            file_path: dir_path.to_path_buf(),
            children: vec,
        })
    }

    pub(crate) fn from(path: impl AsRef<Path>, real_path: PathBuf) -> Result<TreeNode, String> {
        let path = path.as_ref();
        let head_path = Self::get_head_path(path)?;

        Self::from_tree(&path.to_path_buf(), head_path, real_path)
    }

    fn from_tree(
        working_directory: &PathBuf,
        path: impl AsRef<Path>,
        real_path: PathBuf,
    ) -> Result<TreeNode, String> {
        let path = path.as_ref();
        let mut data = Self::read_file(path)?;

        let mut children: Vec<Node> = Vec::new();

        while !data.is_empty() {
            let entry_type: [u8; 6];
            let file_name: String;
            let hash: [u8; 32];
            (data, (entry_type, file_name, hash)) = Self::parse_header(data)?;

            let child_real_path = real_path.join(file_name);

            let child_path = Self::hash_to_path(working_directory, &hash);

            match &entry_type {
                Self::EXECUTABLE_FILE | Self::REGULAR_FILE => {
                    match LeafNode::from(child_path, child_real_path) {
                        Ok(node) => children.push(Leaf(node)),
                        Err(e) => return Err(format!("{} at {}", e, real_path.display())),
                    }
                }
                Self::DIRECTORY => {
                    match TreeNode::from_tree(working_directory, child_path, child_real_path) {
                        Ok(node) => children.push(Tree(node)),
                        Err(e) => return Err(format!("{} at {}", e, real_path.display())),
                    }
                }
                _ => Err("Invalid entry type")?,
            }
        }

        Ok(TreeNode {
            hash: Self::hash_tree(&mut children),
            children,
            file_path: real_path.to_path_buf(),
        })
    }

    pub(crate) fn apply_diff(&mut self, diffs: Vec<Diff>) -> Result<(), String> {
        for diff in diffs {
            match diff {
                Diff::Created { node } => match self.insert(node.clone()) {
                    Err((_, e)) => return Err(e),
                    _ => continue,
                },
                Diff::Deleted { file_path } => self.remove(&file_path)?,
                Diff::Changed { file_path, changes } => {
                    self.apply_blob(&file_path, changes)?;
                }
            }
        }

        self.recompute_tree();

        // if !self.save_tree() {
        //     return Err(String::from(
        //         "Failed to write tree to disk after applying diffs",
        //     ));
        // }

        Ok(())
    }

    fn insert(&mut self, mut node: Node) -> Result<(), (Node, String)> {
        let node_path = node.get_path().clone();
        let parent_dir = match node_path.parent() {
            Some(dir) => dir,
            None => {
                return Err((
                    node,
                    format!(
                        "Unable to get parent directory of node of path: {}",
                        node_path.display()
                    ),
                ));
            }
        };
        if parent_dir == self.file_path {
            self.children.push(node);
            return Ok(());
        }

        for child in &mut self.children {
            match child {
                Tree(tree) => match tree.insert(node) {
                    Ok(_) => return Ok(()),

                    Err((node_val, _)) => {
                        node = node_val;
                    }
                },
                _ => continue,
            }
        }

        Err((
            node,
            format!("Unable to insert node of path: {}", node_path.display()),
        ))
    }

    fn remove(&mut self, file_path: &PathBuf) -> Result<(), String> {
        let parent_dir = match file_path.parent() {
            Some(dir) => dir,
            None => {
                return Err(format!(
                    "Unable to get parent directory of file of path: {}",
                    file_path.display()
                ));
            }
        };
        if parent_dir == self.file_path {
            let original_size = self.children.len();
            self.children.retain(|x| x.get_path() != file_path);
            if original_size == self.children.len() {
                return Err(format!(
                    "Unable to remove node of path:{}",
                    file_path.display()
                ));
            }
            return Ok(());
        }

        for child in &mut self.children {
            match child {
                Tree(tree) => match tree.remove(file_path) {
                    Ok(_) => {
                        return Ok(());
                    }
                    Err(_) => continue,
                },
                _ => continue,
            }
        }

        Err(format!(
            "Unable to remove node of path: {}",
            file_path.display()
        ))
    }

    fn apply_blob(&mut self, file_path: &Path, changes: Vec<Change>) -> Result<(), String> {
        let parent_dir = match file_path.parent() {
            Some(dir) => dir,
            None => {
                return Err(format!(
                    "Unable to get parent directory of file of path: {}",
                    file_path.display()
                ));
            }
        };

        if parent_dir == self.file_path {
            for child in &mut self.children {
                match child {
                    Leaf(leaf) => {
                        if leaf.file_path == file_path.to_path_buf() {
                            return match leaf.apply_blob(changes) {
                                Ok(_) => Ok(()),

                                Err(_) => Err(format!(
                                    "Unable to apply blob changes to file of path:{}",
                                    file_path.display()
                                )),
                            };
                        }
                    }

                    _ => continue,
                }
            }

            return Err(format!(
                "Unable to apply blob changes to file of path:{}",
                file_path.display()
            ));
        }

        Err(format!(
            "Unable to apply blob changes to file of path:{}",
            file_path.display()
        ))
    }

    fn recompute_tree(&mut self) {
        for child in &mut self.children {
            match child {
                Tree(tree) => tree.recompute_tree(),
                _ => continue,
            }
        }

        self.hash = TreeNode::hash_tree(&mut self.children);
    }
}

impl EntryData for TreeNode {}

impl Hashable for TreeNode {
    fn hash(vec: &[u8]) -> [u8; 32] {
        use sha2::{Digest, Sha256};
        Sha256::digest(vec).into()
    }

    fn get_hash(&self) -> [u8; 32] {
        self.hash
    }
}

impl HashableNode for TreeNode {
    fn hash_tree(children: &mut [Node]) -> [u8; 32] {
        children.sort_by(|a, b| a.get_path().cmp(b.get_path()));

        let mut data: Vec<u8> = Vec::with_capacity(children.len() * 32);

        for child in children.iter() {
            let children_hash = child.get_hash();
            data.extend_from_slice(&children_hash);
        }

        Self::hash(data.as_slice())
    }
}

impl TreeIO for TreeNode {
    fn save_tree(&self) -> Result<(), String> {
        self.init()?;
        if !self.write_tree(&self.file_path) {
            return Err("Unable to write tree file".to_string());
        }

        let path = self.file_path.join(Self::HEAD_FILE);
        let branch = match path.exists() {
            true => match fs::read(path) {
                Ok(head) => match String::from_utf8(head) {
                    Ok(str) => str,
                    Err(_) => return Err("Unable to convert string from utf8".to_string()),
                },
                Err(_) => return Err("Unable to read contents of head file".to_string()),
            },
            false => Self::DEFAULT_BRANCH.to_string(),
        };

        match self.write_file(self.file_path.join(Self::HEAD_FILE), &branch) {
            true => match self.write_file(
                self.file_path.join(Self::BRANCH_FOLDER).join(branch),
                self.hash,
            ) {
                true => Ok(()),
                false => Err("Unable to write selected branch to head file".to_string()),
            },

            false => Err("Unable to write hash to branch file".to_string()),
        }
    }
}

impl TreeIOInternal for TreeNode {
    fn init(&self) -> Result<(), String> {
        let paths = [Self::MAIN_FOLDER, Self::OBJ_FOLDER, Self::BRANCH_FOLDER];
        for path in paths.iter() {
            let path = self.file_path.join(path);

            if !path.exists() && fs::create_dir_all(path).is_err() {
                return Err("Unable to create new tree directory".to_string());
            }
        }
        Ok(())
    }

    fn write_tree(&self, cwd: impl AsRef<Path>) -> bool {
        let cwd = cwd.as_ref().to_path_buf();
        let obj_folder = cwd.join(Self::OBJ_FOLDER);
        let path = cwd
            .join(Self::OBJ_FOLDER)
            .join(&Self::hash_to_hex_string(&self.hash)[..2]);
        if !path.exists() {
            match fs::create_dir_all(&path) {
                Ok(_) => (),
                Err(_) => eprintln!("Unable to create object folder"),
            }
        }

        let parent_file = path.join(&Self::hash_to_hex_string(&self.hash)[2..]);
        if parent_file.exists() {
            return true;
        }
        let mut data: Vec<u8> = Vec::new();
        for child in self.children.iter() {
            let filename = match child.get_filename() {
                Ok(filename) => filename,
                Err(_) => return false,
            };
            let entry = match child {
                Leaf(child) => {
                    match child.write_blob(&obj_folder) {
                        Ok(_) => (),
                        Err(_) => {
                            eprintln!("Error writing blob to disk: {}", child.file_path.display());
                            return false;
                        }
                    }
                    match child.is_executable() {
                        Ok(boolean) => match boolean {
                            true => Self::EXECUTABLE_FILE.as_slice(),
                            false => Self::REGULAR_FILE.as_slice(),
                        },
                        Err(e) => {
                            eprintln!("{}", e);
                            return false;
                        }
                    }
                }
                Tree(child) => {
                    if !child.write_tree(&cwd) {
                        eprintln!("Error writing tree to disk: {}", child.file_path.display());
                        return false;
                    }
                    Self::DIRECTORY.as_slice()
                }
            };

            data.extend_from_slice(entry);
            data.push(b' ');
            data.extend_from_slice(filename.as_bytes());
            data.push(0);
            data.extend_from_slice(&child.get_hash());
        }
        self.write_file(&parent_file, data);
        true
    }

    fn parse_header(mut data: Vec<u8>) -> Result<(Vec<u8>, Header), String> {
        if data.is_empty() {
            return Err("Buffer is empty".to_string());
        }

        let entry_type: [u8; 6] = match data.drain(0..6).collect::<Vec<u8>>().try_into() {
            Ok(entry) => entry,
            Err(_) => return Err("Unable to parse header".to_string()),
        };

        data.remove(0);
        let file_name: Vec<u8>;
        (file_name, data) = match Self::read_until_null(data) {
            Ok((file_name, data)) => (file_name, data),
            Err(_) => return Err("Unable to parse header".to_string()),
        };

        let hash: [u8; 32] = match data.drain(0..32).collect::<Vec<u8>>().try_into() {
            Ok(entry) => entry,
            Err(_) => return Err("Unable to parse header".to_string()),
        };

        let file_name = match String::from_utf8(file_name) {
            Ok(file_name) => file_name,
            Err(_) => return Err("Unable to convert filename to valid UTF8 String".to_string()),
        };

        Ok((data, (entry_type, file_name, hash)))
    }
}
impl ReadFile for TreeNode {}
