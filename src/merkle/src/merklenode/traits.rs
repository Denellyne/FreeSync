use crate::merklenode::diff::Change;
use crate::merklenode::node::Node;
use crate::merklenode::traits::internal_traits::TreeIOInternal;
use crate::traits::{CompressedData, Hashable, ReadFile};
use std::path::{Path, PathBuf};
use tempfile::NamedTempFile;

pub(crate) trait HashableNode: Hashable + TreeIO {
    fn hash_tree(vec: &mut [Node]) -> [u8; 32];
}

pub(super) trait EntryData {
    const REGULAR_FILE: &'static [u8; 6] = b"100000";
    const EXECUTABLE_FILE: &'static [u8; 6] = b"100755";
    // const SYMBOLIC_LINK: &'static [u8; 6] = b"120000";
    const DIRECTORY: &'static [u8; 6] = b"040000";
}

pub(super) type Header = ([u8; 6], String, [u8; 32]);

pub(super) mod internal_traits {
    use crate::{merklenode::traits::Header, traits::IO};
    use std::path::Path;

    pub trait TreeIOInternal: IO {
        fn init(&self) -> Result<(), String>;

        fn write_tree(&self, cwd: impl AsRef<Path>) -> bool;

        fn parse_header(data: Vec<u8>) -> Result<(Vec<u8>, Header), String>;
    }
}

pub trait TreeIO: TreeIOInternal + ReadFile {
    const MAIN_FOLDER: &'static str = ".freesync";
    const OBJ_FOLDER: &'static str = ".freesync/objects";
    const BRANCH_FOLDER: &'static str = ".freesync/branch";
    const DEFAULT_BRANCH: &'static str = "main";
    const HEAD_FILE: &'static str = ".freesync/HEAD";
    const UPSTREAM_FILE: &'static str = ".freesync/UPSTREAM";

    fn save_tree(&self) -> Result<(), String>;
    fn get_head_path(path: impl AsRef<Path>) -> Result<PathBuf, String> {
        let path = path.as_ref();

        let head_file = path.join(Self::HEAD_FILE);
        let branch: String = match Self::read_file(head_file)?.try_into() {
            Ok(it) => it,
            Err(_) => return Err(format!("Unable to read file:{}", path.display())),
        };
        let head_file = path.join(Self::BRANCH_FOLDER).join(branch);
        let data: [u8; 32] = match Self::read_file(head_file)?.try_into() {
            Ok(it) => it,
            Err(_) => return Err(format!("Unable to read file:{}", path.display())),
        };

        Ok(Self::hash_to_path(path, &data))
    }
    fn hash_to_path(path: impl AsRef<Path>, hash: &[u8; 32]) -> PathBuf {
        let path = path.as_ref().join(Self::OBJ_FOLDER);

        let header = Node::hash_to_hex_string(hash);
        let child_folder = Path::new(&header[..2]);
        let child_file = Path::new(&header[2..]);
        path.join(child_folder).join(child_file)
    }
    fn save_head(&self) -> Result<(), String>;
    fn save_upstream(&self) -> Result<(), String>;
}

pub(crate) trait LeafIO: CompressedData + ReadFile {
    fn write_blob(&self, path: &Path) -> Result<(), String>;
    fn is_executable(&self) -> Result<bool, String>;
    fn atomic_write_file(&self, path: &Path, data: &[u8]) -> Result<NamedTempFile, String>;
    fn atomic_rename(&self, file: &Path, path: &Path) -> Result<(), String>;
    fn data(&self) -> &Vec<u8>;

    fn diff_file(&self, other: &Self) -> Result<Vec<Change>, String>;

    fn decompress_data(data: &[u8]) -> Result<Vec<u8>, String>;

    fn hash_file(path: impl AsRef<Path>) -> Result<([u8; 32], Vec<u8>), String> {
        let file_contents = Self::read_file(&path);
        match file_contents {
            Ok(contents) => {
                let hash = Node::hash(&contents);
                Ok((hash, contents))
            }
            _ => Err(format!("Unable to read file {}", path.as_ref().display())),
        }
    }
}
