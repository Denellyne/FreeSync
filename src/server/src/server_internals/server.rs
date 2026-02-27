use logger::Logger;
use merkle::merklenode::node::Node;
use merkle::merklenode::traits::TreeIO;
use merkle::merkletree::MerkleTree;
use std::io::{BufRead, BufReader};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::sync::mpsc::Sender;

use crate::server_internals::threadpool::ThreadPool;
// The only uses of expect and unwrap should be at the startup,after that there shall be no unwraps
pub struct Server {
    listener: TcpListener,
    tree: Arc<Node>,
    tx: Sender<String>,
}

impl Server {
    pub fn new(port: String) -> Self {
        let tx = Logger::create(
            "./logs/server.log",
            "Server".parse().expect("Unable to parse string"),
            true,
            true,
        );

        let _ = tx.send("Starting the Server...".to_string());

        if port.parse::<u16>().is_err() {
            panic!("Invalid port number!");
        }

        let ip = format!("0.0.0.0:{}", port);
        let listener = TcpListener::bind(ip).expect("Could not bind port!");
        let _ = tx.send("Port bound".to_string());
        let tree = MerkleTree::create("./".into())
            .expect("Unable to generate the merkle tree for the current working directory");
        tree.save_tree().expect("Unable to save the tree to disk");
        let tree = Node::Tree(tree);
        let head_path = MerkleTree::get_head_path("./".into()).expect("Unable to get head path");
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
                    pool.execute(move || handle_connection(stream, tx_clone));
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

    #[cfg(test)]
    pub(super) fn mock_server(self) -> Vec<String> {
        println!("\nServer running\n");
        let mut request: Vec<String> = Vec::new();
        if let Some(stream) = self.listener.incoming().next() {
            match stream {
                Ok(stream) => {
                    request = mock_handle_connection(stream);
                    self.close_server();
                    return request;
                }
                Err(e) => panic!("Unable to establish connection, {}", e),
            }
        }
        request
    }
}

#[cfg(test)]
fn mock_handle_connection(stream: TcpStream) -> Vec<String> {
    let buf_reader = BufReader::new(&stream);

    let request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap_or_default())
        .take_while(|line| !line.is_empty())
        .collect();
    request
}
fn handle_connection(stream: TcpStream, tx: Sender<String>) {
    let buf_reader = BufReader::new(&stream);

    let request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap_or_default())
        .take_while(|line| !line.is_empty())
        .collect();

    let _ = tx.send(format!("Request: {:?}", request));

    // log_fmt!(log, "Request: {:?}", request);
}
