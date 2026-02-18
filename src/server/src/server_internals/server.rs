use logger::Logger;
use merkle::merklenode::node::Node;
use merkle::merklenode::traits::TreeIO;
use merkle::merkletree::MerkleTree;
use std::io::{BufRead, BufReader};
use std::net::{TcpListener, TcpStream};
// The only uses of expect and unwrap should be at the startup,after that there shall be no unwraps
pub struct Server {
    listener: TcpListener,
    tree: Node,
    logger: Logger,
}

impl Server {
    pub fn new(port: String) -> Self {
        let mut logger = Logger::new(
            "./logs/server.log",
            "Server".parse().expect("Unable to parse string"),
            true,
            true,
        )
        .expect("Unable to open logger for server");

        logger.log("Starting the Server...".to_string());

        if port.parse::<u16>().is_err() {
            panic!("Invalid port number!");
        }

        let ip = format!("0.0.0.0:{}", port);
        let listener = TcpListener::bind(ip).expect("Could not bind port!");
        logger.log("Port bound".to_string());
        let tree = MerkleTree::create("./".into())
            .expect("Unable to generate the merkle tree for the current working directory");
        tree.save_tree().expect("Unable to save the tree to disk");
        let tree = Node::Tree(tree);
        let head_path = MerkleTree::get_head_path("./".into()).expect("Unable to get head path");

        logger.log(format!(
            "Info:\nFreeSync Server\nIp:{}\nCurrent branch:{}\nCurrent hash:{}",
            listener.local_addr().expect("Could not get local address"),
            head_path.clone().display(),
            MerkleTree::get_branch_hash(head_path).expect("Unable go get branch hash")
        ));
        logger.log("Server started".to_string());
        Server {
            listener,
            logger,
            tree,
        }
    }

    fn close_server(mut self) {
        self.logger.log("\nClosing the Server...".to_string());
        self.logger.log("Server closed".to_string());
    }

    pub fn run_server(mut self) {
        self.logger.log("\nServer running\n".to_string());
        for stream in self.listener.incoming() {
            match stream {
                Ok(stream) => {
                    let request = handle_connection(stream);
                    self.logger.log(format!("Request: {:?}", request));
                }
                Err(e) => self
                    .logger
                    .log(format!("Unable to establish connection, {e}")),
            }
        }

        self.close_server();
    }

    #[cfg(test)]
    pub(super) fn mock_server(self) -> Vec<String> {
        println!("\nServer running\n");
        let mut request: Vec<String> = Vec::new();
        if let Some(stream) = self.listener.incoming().next() {
            match stream {
                Ok(stream) => {
                    request = handle_connection(stream);
                    self.close_server();
                    return request;
                }
                Err(e) => panic!("Unable to establish connection, {}", e),
            }
        }
        request
    }
}

fn handle_connection(stream: TcpStream) -> Vec<String> {
    let buf_reader = BufReader::new(&stream);

    let request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap())
        .take_while(|line| !line.is_empty())
        .collect();
    request
}
