#[derive(Debug)]
pub(crate) enum MerkleEntry {
    Blob {
        data: Vec<u8>,
        size: u64,
        file_name: String,
        mode: &'static [u8; 6],
    },
    Tree {
        hash: [u8; 32],
        file_name: String,
        entries: Vec<MerkleEntry>,
    },
}

impl MerkleEntry {}
