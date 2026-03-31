use merkle::data::{Packet, serialize};
use merkle::merklenode::traits::TreeIO;
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
use merkle::traits::ReadFile;
use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::sync::{Arc, Mutex};
use threadpool::pool::ThreadPool;

// The only uses of expect and unwrap should be at the startup,after that there shall be no unwraps
pub struct Server {
    listener: TcpListener,
    pub(super) mutex: Arc<Mutex<()>>,
    tx: Sender<String>,
    path: PathBuf,
}

impl Server {
    pub fn new(port: String, path: PathBuf, tx: Sender<String>) -> Self {
        let _ = tx.send("Starting the Server...".to_string());

        if port.parse::<u16>().is_err() {
            panic!("Invalid port number!");
        }

        let ip = format!("0.0.0.0:{port}");
        let listener = TcpListener::bind(ip).expect("Could not bind port!");
        let _ = tx.send("Port bound".to_string());

        let head_path =
            MerkleTree::get_head_path(path.to_path_buf()).expect("Unable to get head path");

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
            path,
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
                    let path_clone = self.path.clone();
                    pool.execute(move || {
                        Self::handle_connection(stream, mutex, tx_clone, &path_clone)
                    });
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

    fn handle_connection(
        stream: TcpStream,
        mutex: Arc<Mutex<()>>,
        tx: Sender<String>,
        path: &PathBuf,
    ) {
        let buf_reader = BufReader::new(&stream);

        let request: Vec<_> = buf_reader
            .lines()
            .map(|result| result.unwrap())
            .take_while(|line| !line.is_empty())
            .collect();

        let _ = tx.send(format!("Request: {:?}", request));

        if request[0] == "CLONE" {
            Server::clone_command(stream, mutex, tx, path)
        } else if request[0] == "GET UPSTREAM" {
            Server::upstream_command(stream, tx, path)
        }
    }
    fn upstream_command(mut stream: TcpStream, tx: Sender<String>, path: &Path) {
        let hash: String = match MerkleTree::get_branch_hash_str(path.to_path_buf()) {
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
    fn clone_command(
        mut stream: TcpStream,
        mutex: Arc<Mutex<()>>,
        tx: Sender<String>,
        path: impl AsRef<Path>,
    ) {
        let lock = mutex.lock().expect("Could not lock mutex");
        let objects: Vec<Packet> = match MerkleTree::get_objects(path.as_ref().to_path_buf()) {
            Ok(data) => data,
            Err(e) => {
                let _ = tx.send(format!("Error while getting objects {e}"));
                return;
            }
        };
        drop(lock);

        let packets = objects.len() + 2;
        if let Err(e) = stream.write_all(format!("{packets}\n").as_bytes()) {
            let _ = tx.send(format!("Error while sending number of packets {e}"));
            return;
        }
        println!("Sending objects");
        for object in objects {
            if let Err(e) = stream.write_all(&serialize(object)) {
                let _ = tx.send(format!("Error while sending packet {e}"));
                return;
            }
        }
        if let Err(e) = Server::send_head(&stream, &tx, path.as_ref()) {
            eprintln!("{e}");
            return;
        };
        if let Err(e) = Server::send_branch(&stream, &tx, path.as_ref()) {
            eprintln!("{e}");
        };
    }

    fn send_branch(mut stream: &TcpStream, tx: &Sender<String>, path: &Path) -> Result<(), String> {
        println!("Sending branch file");
        let path = path
            .join(TreeNode::BRANCH_FOLDER)
            .join(TreeNode::DEFAULT_BRANCH);

        let branch: Vec<u8> = match MerkleTree::read_file(path) {
            Ok(data) => data[..32].to_vec(),
            Err(e) => return Err(e.to_string()),
        };
        let object: Packet = Packet::BranchFile(branch, "main".to_string());
        if let Err(e) = stream.write_all(&serialize(object)) {
            let _ = tx.send(format!("Error while sending branch file {e}"));
            return Err(format!("Error while sending branch file {e}"));
        }
        Ok(())
    }

    fn send_head(mut stream: &TcpStream, tx: &Sender<String>, path: &Path) -> Result<(), String> {
        println!("Sending head file");
        let head_file = path.join(TreeNode::HEAD_FILE);
        let head = match MerkleTree::read_file(&head_file) {
            Ok(it) => match String::from_utf8(it) {
                Ok(it) => it,
                Err(e) => {
                    let _ = tx.send(format!("Error while sending {e}"));
                    return Err(format!("Error while sending {e}"));
                }
            },
            Err(_) => {
                let _ = tx.send(format!("Unable to read file:{}", head_file.display()));
                return Err(format!("Unable to read file:{}", head_file.display()));
            }
        };
        let object: Packet = Packet::HeadFile(head);
        if let Err(e) = stream.write_all(&serialize(object)) {
            let _ = tx.send(format!("Error while sending head file{e}"));
            return Err(format!("Error while sending head file{e}"));
        }
        Ok(())
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
                    request = Self::mock_handle_connection(stream, mutex, self.tx, self.path);
                    return request;
                }
                Err(e) => panic!("Unable to establish connection, {}", e),
            }
        }
        request
    }
    fn mock_handle_connection(
        mut stream: TcpStream,
        mutex: Arc<Mutex<()>>,
        tx: Sender<String>,
        path: PathBuf,
    ) -> Vec<String> {
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
            Server::clone_command(stream, mutex, tx, path);
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
