use crate::merklenode::node::Node;
use std::path::PathBuf;

#[derive(PartialEq, Debug)]
pub(crate) enum Diff {
    Created {
        node: Node,
    },
    Deleted {
        file_path: PathBuf,
    },
    Changed {
        file_path: PathBuf,
        changes: Vec<Change>,
    },
}
#[derive(PartialEq, Debug)]
pub(crate) enum Change {
    Copy { start: u64, end: u64 },
    Delete { start: u64, end: u64 },
    Insert { data: Vec<u8> },
    End { final_hash: [u8; 32] },
}
