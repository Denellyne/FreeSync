use merkle::data::Packet;
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
use merkle::traits::Hashable;
use std::env;
use std::io::{Read, Write};
use std::net::TcpStream;

pub struct Client {
    stream: TcpStream,
}

impl Client {
    fn new() -> Result<Client, String> {
        let dir = match env::current_dir() {
            Ok(dir) => dir,
            Err(e) => return Err(e.to_string()),
        };

        let addr = match MerkleTree::get_upstream(dir) {
            Ok(addr) => addr,
            Err(e) => panic!("{e}"),
        };
        let stream = TcpStream::connect(addr).unwrap();
        Ok(Client { stream })
    }

    pub(crate) fn clone() -> Result<(), String> {
        let mut conn = Client::new()?;
        let command = "CLONE\n\n";

        conn.stream.write_all(command.as_bytes()).unwrap();

        let mut packets: String = String::new();
        conn.stream
            .read_to_string(&mut packets)
            .expect("Could not read from stream");
        let packets = packets
            .parse::<i32>()
            .expect("Could not parse packets into a number");

        for _ in 0..packets {
            let mut buf: Vec<u8> = Vec::new();
            conn.stream.read_to_end(&mut buf).unwrap();
            let packet =
                bincode::deserialize::<Packet>(&buf).expect("Unable to deserialize packet");
            MerkleTree::write_packet(".".into(), packet).expect("Unable to write packet");
        }

        Ok(())
    }
    pub(crate) fn pull() -> Result<(), String> {
        let dir = match env::current_dir() {
            Ok(dir) => dir,
            Err(e) => return Err(e.to_string()),
        };

        let node = MerkleTree::create(dir).expect("Unable to create tree");
        let hash = TreeNode::hash_to_hex_string(&node.get_hash());
        let addr = match MerkleTree::get_upstream(".".into()) {
            Ok(addr) => addr,
            Err(e) => panic!("{e}"),
        };

        let mut conn = Client::new()?;

        let command = "GET UPSTREAM\n\n";

        conn.stream.write_all(command.as_bytes()).unwrap();

        let mut upstream_hash: String = String::new();
        conn.stream.read_to_string(&mut upstream_hash).unwrap();
        println!("{upstream_hash}");

        Ok(())
    }
}
