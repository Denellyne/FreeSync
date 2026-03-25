use crate::threadpool::ThreadPool;
use merkle::data::Packet;
use merkle::merklenode::traits::TreeIO;
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
use merkle::traits::ReadFile;
use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::path::Path;
use std::sync::mpsc::Sender;
use std::sync::{Arc, Mutex};

// The only uses of expect and unwrap should be at the startup,after that there shall be no unwraps
pub struct Server {
    listener: TcpListener,
    pub(super) mutex: Arc<Mutex<()>>,
    tx: Sender<String>,
}

impl Server {
    pub fn new(port: String, path: impl AsRef<Path>, tx: Sender<String>) -> Self {
        let _ = tx.send("Starting the Server...".to_string());

        if port.parse::<u16>().is_err() {
            panic!("Invalid port number!");
        }

        let ip = format!("0.0.0.0:{port}");
        let listener = TcpListener::bind(ip).expect("Could not bind port!");
        let _ = tx.send("Port bound".to_string());

        let head_path = MerkleTree::get_head_path(path.as_ref().to_path_buf())
            .expect("Unable to get head path");

        let _ = tx.send(format!(
            "Info:\nFreeSync Server\nIp:{}\nCurrent branch:{}\nCurrent hash:{}",
            listener.local_addr().expect("Could not get local address"),
            head_path.clone().display(),
            MerkleTree::get_branch_hash(head_path).expect("Unable go get branch hash")
        ));

        let _ = tx.send("Server started".to_string());
        let mutex = Arc::new(Mutex::new(()));

        Server {
            listener,
            mutex,
            tx,
        }
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
                    let mutex = Arc::clone(&self.mutex);
                    pool.execute(move || Self::handle_connection(stream, mutex, tx_clone));
                }
                Err(e) => {
                    if let Err(e) = self.tx.send(format!("Unable to establish connection, {e}")) {
                        eprintln!("{e}")
                    }
                }
            }
        }
        pool.join_with_timeout(30000);

        self.close_server();
    }

    fn handle_connection(stream: TcpStream, mutex: Arc<Mutex<()>>, tx: Sender<String>) {
        let buf_reader = BufReader::new(&stream);

        let request: Vec<_> = buf_reader
            .lines()
            .map(|result| result.unwrap())
            .take_while(|line| !line.is_empty())
            .collect();

        let _ = tx.send(format!("Request: {:?}", request));

        if request[0] == "CLONE" {
            Server::clone_command(stream, mutex, tx)
        } else if request[0] == "GET UPSTREAM" {
            Server::upstream_command(stream, tx)
        }
    }
    fn upstream_command(mut stream: TcpStream, tx: Sender<String>) {
        let hash: String = match MerkleTree::get_branch_hash_str(".".into()) {
            Ok(data) => data + "\n",
            Err(e) => {
                let _ = tx.send(format!("Error while sending {e}"));
                return;
            }
        };
        if let Err(e) = stream.write_all(hash.as_bytes()) {
            let _ = tx.send(format!("Error while sending {e}"));
        }
    }
    fn clone_command(mut stream: TcpStream, mutex: Arc<Mutex<()>>, tx: Sender<String>) {
        let _lock = mutex.lock().expect("Could not lock mutex");
        let objects: Vec<Packet> = MerkleTree::get_objects(".".into()).unwrap();
        let packets = objects.len() + 2;
        if let Err(e) = stream.write_all(format!("{packets}\n").as_bytes()) {
            let _ = tx.send(format!("Error while sending {e}"));
            return;
        }
        for object in objects {
            let buf = bincode::serialize(&object).expect("Could not serialize object");
            if let Err(e) = stream.write_all(&buf) {
                let _ = tx.send(format!("Error while sending {e}"));
            }
        }
        Server::send_head(&stream, &tx);
        Server::send_branch(&stream, &tx);
    }

    fn send_branch(mut stream: &TcpStream, tx: &Sender<String>) {
        let branch = match MerkleTree::get_branch_hash_str(".".into()) {
            Ok(it) => it,
            Err(e) => {
                let _ = tx.send(format!("Error while sending {e}"));
                return;
            }
        };
        let object = Packet::HeadFile(branch);
        let buf = bincode::serialize(&object).expect("Could not serialize object");
        if let Err(e) = stream.write_all(&buf) {
            let _ = tx.send(format!("Error while sending {e}"));
        }
    }

    fn send_head(mut stream: &TcpStream, tx: &Sender<String>) {
        let head_file = Path::new(".").join(TreeNode::HEAD_FILE);
        let head = match MerkleTree::read_file(&head_file) {
            Ok(it) => match String::from_utf8(it) {
                Ok(it) => it,
                Err(e) => {
                    let _ = tx.send(format!("Error while sending {e}"));
                    return;
                }
            },
            Err(_) => {
                let _ = tx.send(format!("Unable to read file:{}", head_file.display()));
                return;
            }
        };
        let object = Packet::HeadFile(head);
        let buf = bincode::serialize(&object).expect("Could not serialize object");
        if let Err(e) = stream.write_all(&buf) {
            let _ = tx.send(format!("Error while sending {e}"));
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
                    let mutex = Arc::clone(&self.mutex);
                    request = Self::mock_handle_connection(stream, mutex);
                    return request;
                }
                Err(e) => panic!("Unable to establish connection, {}", e),
            }
        }
        request
    }
    fn mock_handle_connection(mut stream: TcpStream, mutex: Arc<Mutex<()>>) -> Vec<String> {
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
            let _lock = mutex.lock().expect("Could not lock mutex");
            // let buf = bincode::serialize(&*node).unwrap();
            // println!("{:?}", buf);
            // if let Err(e) = stream.write_all(&buf) {
            //     panic!("{e}");
            // }
            // let hash = MerkleTree::get_branch_hash_str(".".into()).unwrap();
            // println!("Hash:{hash}");
            // return vec![hash];
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
