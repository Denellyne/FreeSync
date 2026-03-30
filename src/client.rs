use merkle::data::deserialize_from_stream;
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
use merkle::traits::Hashable;
use std::env;
use std::io::{BufRead, BufReader, Read, Write};
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
            Err(e) => return Err(e.to_string()),
        };
        let stream = TcpStream::connect(&addr)
            .unwrap_or_else(|_| panic!("Failed to connect to server,Upstream : {addr}"));
        Ok(Client { stream })
    }

    pub(crate) fn clone() -> Result<(), String> {
        let mut conn = Client::new()?;
        let command = "CLONE\n\n";

        conn.stream
            .write_all(command.as_bytes())
            .expect("Unable to write command to stream");

        let mut packets: String = String::new();
        let mut reader = BufReader::new(&conn.stream);
        reader.read_line(&mut packets).unwrap();
        let _ = packets.pop();
        drop(reader);
        let packets = packets
            .parse::<i32>()
            .expect("Could not parse packets into a number");
        println!("Objects {packets}");

        for _ in 0..packets {
            let packet = match deserialize_from_stream(&mut conn.stream) {
                Ok(data) => data,
                Err(e) => return Err(e.to_string()),
            };

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
        let _hash = TreeNode::hash_to_hex_string(&node.get_hash());
        let _addr = match MerkleTree::get_upstream(".".into()) {
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
