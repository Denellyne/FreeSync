use merkle::merklenode::node::Node;
use merkle::merklenode::traits::TreeIO;
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
use merkle::traits::Hashable;
use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::path::Path;
use std::sync::Arc;
use std::sync::mpsc::Sender;

use crate::server_internals::threadpool::ThreadPool;
// The only uses of expect and unwrap should be at the startup,after that there shall be no unwraps
pub struct Server {
    listener: TcpListener,
    pub(super) tree: Arc<Node>,
    tx: Sender<String>,
}

impl Server {
    pub fn new(port: String, path: impl AsRef<Path>, tx: Sender<String>) -> Self {
        let _ = tx.send("Starting the Server...".to_string());

        if port.parse::<u16>().is_err() {
            panic!("Invalid port number!");
        }

        let ip = format!("0.0.0.0:{}", port);
        let listener = TcpListener::bind(ip).expect("Could not bind port!");
        let _ = tx.send("Port bound".to_string());
        let tree = MerkleTree::create(path.as_ref().to_path_buf())
            .expect("Unable to generate the merkle tree for the current working directory");
        tree.save_tree().expect("Unable to save the tree to disk");

        let tree = Node::Tree(tree);
        let head_path = MerkleTree::get_head_path(path.as_ref().to_path_buf())
            .expect("Unable to get head path");

        let _ = tx.send(format!(
            "Info:\nFreeSync Server\nIp:{}\nCurrent branch:{}\nCurrent hash:{}",
            listener.local_addr().expect("Could not get local address"),
            head_path.clone().display(),
            MerkleTree::get_branch_hash(head_path).expect("Unable go get branch hash")
        ));

        let _ = tx.send("Server started".to_string());
        let tree = Arc::from(tree);

        Server { listener, tree, tx }
    }

    fn close_server(self) {
        let _ = self.tx.send("\nClosing the Server...".to_string());
        let _ = self.tx.send("Server closed".to_string());
    }

    pub fn run_server(self) {
        let _ = self.tx.send("\nServer running\n".to_string());

        let mut pool = ThreadPool::new(4);

        for stream in self.listener.incoming() {
            match stream {
                Ok(stream) => {
                    let tx_clone = self.tx.clone();
                    let node = Arc::clone(&self.tree);
                    pool.execute(move || Self::handle_connection(stream, node, tx_clone));
                }
                Err(e) => match self.tx.send(format!("Unable to establish connection, {e}")) {
                    Ok(_) => (),
                    Err(e) => {
                        eprintln!("{e}")
                    }
                },
            }
        }
        pool.join_with_timeout(30000);

        self.close_server();
    }

    fn handle_connection(mut stream: TcpStream, node: Arc<Node>, tx: Sender<String>) {
        let buf_reader = BufReader::new(&stream);

        let request: Vec<_> = buf_reader
            .lines()
            .map(|result| result.unwrap())
            .take_while(|line| !line.is_empty())
            .collect();

        let _ = tx.send(format!("Request: {:?}", request));

        if request[0] == "CLONE" {
            let buf = bincode::serialize(&*node).unwrap();
            if let Err(e) = stream.write_all(&buf) {
                let _ = tx.send(format!("Error while sending {e}"));
            }
        } else if request[0] == "GET UPSTREAM" {
            let hash = TreeNode::hash_to_hex_string(&node.get_hash());
            let hash = hash + "\n";
            if let Err(e) = stream.write_all(hash.as_bytes()) {
                let _ = tx.send(format!("Error while sending {e}"));
            }
        }
    }
}

#[cfg(test)]
impl Server {
    pub(super) fn mock_server(self) -> Vec<String> {
        println!("\nServer running\n");
        let mut request: Vec<String> = Vec::new();
        if let Some(stream) = self.listener.incoming().next() {
            match stream {
                Ok(stream) => {
                    let node = Arc::clone(&self.tree);
                    request = Self::mock_handle_connection(stream, node);
                    return request;
                }
                Err(e) => panic!("Unable to establish connection, {}", e),
            }
        }
        request
    }
    fn mock_handle_connection(mut stream: TcpStream, node: Arc<Node>) -> Vec<String> {
        println!("Received connection");
        let buf_reader = BufReader::new(&stream);
        println!("Created bufreader");

        let request: Vec<_> = buf_reader
            .lines()
            .map(|result| result.unwrap())
            .take_while(|line| !line.is_empty())
            .collect();
        println!("Request: {:?}", request);
        if request[0] == "CLONE" {
            use merkle::traits::Hashable;

            let buf = bincode::serialize(&*node).unwrap();
            println!("{:?}", buf);
            if let Err(e) = stream.write_all(&buf) {
                panic!("{e}");
            }
            let hash = Node::hash_to_hex_string(&node.get_hash());
            println!("Hash:{hash}");
            return vec![hash];
        } else if request[0] == "TEST" {
            if let Err(e) = stream.write_all(b"OK") {
                panic!("{e}");
            }
        } else {
            println!("ERROR");
            let _ = stream.write_all(b"ERROR\n");
        }
        request
    }
}
