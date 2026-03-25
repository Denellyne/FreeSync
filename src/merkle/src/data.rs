use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub enum Packet {
    ObjectFile(Vec<u8>, String),
    HeadFile(String),
    BranchFile(Vec<u8>, String),
}
