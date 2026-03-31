use std::fs;
use crate::server::Server;
use logger::mock::MockLogger;
use merkle::data::deserialize_from_stream;
use merkle::merklenode::node::Node;
use merkle::traits::Hashable;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{Shutdown, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::sync::mpsc::Sender;
use std::thread::{self};
use rand::random;
use tempfile::{tempdir_in, NamedTempFile, TempDir};
use merkle::merklenode::tree::TreeNode;
use merkle::merkletree::MerkleTree;
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

 
    pub fn create(path: PathBuf) -> Result<TreeNode, String> {
        match fs::read_dir(&path) {
            Ok(_) => match path {
                path if path.is_dir() => MerkleTree::new_tree(path),
                path if path.is_file() => Err(format!("Path is of a file: {}", path.display())),
                path if path.is_symlink() => Err(format!("Path is a symlink: {}", path.display())),
                _ => Err(String::from("Unable to generate merkle tree")),
            },
            _ => Err(format!(
                "Could not read directory {:?}, is it a path to a directory?",
                &path
            ))?,
        }
    }
 

    pub fn random_tree_builder(
        path: Option<PathBuf>,
    ) -> (Result<Node, String>, Option<TempDir>) {
        match path {
            Some(path) => {
                let (node, _, _) = generate_random_tree(path);
                (node, None)
            }
            None => {
                let temp_dir = tempfile::tempdir().expect("Unable to create temp dir");
                let (node, _, _) = generate_random_tree(temp_dir.path().to_path_buf());
                (node, Some(temp_dir))
            }
        }
    }


    pub fn write_random_to_file(file: NamedTempFile) -> (NamedTempFile, String) {
        let mut str: String = String::new();
        let len = random::<u16>() % u16::MAX / 4 + 1;
        for _i in 0..len {
            str.push(random::<char>());
        }
        write!(&file, "{}", str).expect("Unable to write to file");
        (file, str)
    }

    pub  fn generate_random_file(path: &PathBuf) -> NamedTempFile {
        let (file, _) =
          write_random_to_file(NamedTempFile::new_in(path).expect("Unable to create temporary file"));
        file
    }
    pub fn generate_random_tree(
        path: PathBuf,
    ) -> (Result<Node, String>, Vec<NamedTempFile>, Vec<TempDir>) {
        let size = random::<u8>() % 12 + 1;
        let mut current_path: PathBuf = path.clone();
        let mut temporary_files: Vec<NamedTempFile> = Vec::new();
        let mut temporary_folders: Vec<TempDir> = Vec::new();

        let get_relative_path =
          |str: &Path| -> PathBuf { str.file_name().expect("Unable to get file name").into() };

        for _i in 0..size {
            let gen_dir = random::<bool>();
            if gen_dir {
                let temp_file = tempdir_in(&current_path).expect("Unable to create temporary folder");
                let relative_path = get_relative_path(temp_file.path());
                current_path.push(&relative_path);

                temporary_folders.push(temp_file);
            } else {
                let temp_file =generate_random_file(&current_path);
                temporary_files.push(temp_file);
            }
        }

        let tree = Node::Tree(create(path.to_path_buf()).expect("Unable to create tree"));
        (Ok(tree), temporary_files, temporary_folders)
    }
    pub fn random_data() -> String {
        let mut str: String = String::new();
        let len = random::<u16>() % u16::MAX / 4 + 1;
        for _i in 0..len {
            str.push(random::<char>());
        }
        str
    }


struct MockConnection {
    pub stream: TcpStream,
    pub data: String,
}



impl MockConnection {
    fn new() -> MockConnection {
        let stream = TcpStream::connect(format!("localhost:{}", 25565))
            .expect("Failed to connect to server");
        let data = random_data();

        MockConnection { stream, data }
    }
    fn from(data: String, port: u64) -> MockConnection {
        let stream =
            TcpStream::connect(format!("localhost:{}", port)).expect("Failed to connect to server");
        let data = format! {"{data}\n\n"};

        MockConnection { stream, data }
    }
    fn write(&mut self) {
        self.stream
            .write_all(self.data.as_bytes())
            .expect("Failed to write to stream");
        println!("Sent {}", self.data);
    }
    fn read(&self) -> Vec<u8> {
        println!("Reading...");
        let mut buf_reader = BufReader::new(&self.stream);
        println!("Created buf reader");
        let mut buf: Vec<u8> = Vec::new();
        buf_reader.read_to_end(&mut buf).unwrap();

        println!("Read {:?}", buf);

        buf
    }
    fn get_data(&self) -> String {
        self.data.clone()
    }
    fn close(&self) {
        self.stream
            .shutdown(Shutdown::Both)
            .expect("Failed to shutdown stream");
    }
}


#[test]
fn test_connection() {
    let tx = MockLogger::create();
    let (_tree, folder) = random_tree_builder(None::<PathBuf>);
    let sv = Server::new(
        "25565".parse().unwrap(),
        folder.unwrap().path().to_path_buf(),
        tx,
    );
    let th = thread::spawn(move || sv.mock_server());

    let mut conn = MockConnection::new();
    conn.write();
    conn.close();

    let result = th.join().expect("Failed to join thread");

    assert!(
        conn.get_data()
            .eq(result.first().expect("Failed to get first data"))
    );
}
#[test]
fn test_reply() {
    let tx = MockLogger::create();
    let (_tree, folder) = random_tree_builder(None::<PathBuf>);

    let sv = Server::new(
        "25567".parse().unwrap(),
        folder.unwrap().path().to_path_buf(),
        tx,
    );
    let th = thread::spawn(move || sv.mock_server());

    let mut conn = MockConnection::from("TEST".to_string(), 25567);
    conn.write();
    let data = conn.read();
    conn.close();

    let _result = th.join().expect("Failed to join thread");
    println!("{}", String::from_utf8_lossy(&data));

    assert_eq!(data, "OK".as_bytes());
}

#[test]
fn test_clone() {
    let tx = MockLogger::create();
    let (tree, folder) = random_tree_builder(None::<PathBuf>);
    let tree = match tree {
        Ok(tree) => match tree {
            Node::Tree(tree) => tree,
            Node::Leaf(_) => panic!("Not a tree"),
        },
        Err(e) => panic!("Unable to create tree: {:?}", e),
    };
    let folder = match folder {
        Some(folder) => folder,
        None => panic!("Unable to create folder"),
    };

    let sv = Server::new("25566".parse().unwrap(), folder.path().to_path_buf(), tx);
    let th = thread::spawn(move || sv.mock_server());

    let mut conn = MockConnection::from("CLONE".to_string(), 25566);
    conn.write();

    let mut packets: String = String::new();
    let mut reader = BufReader::new(&mut conn.stream);
    reader
        .read_line(&mut packets)
        .expect("Unable to read from stream");
    let _ = packets.pop();
    let packets = packets
        .parse::<i32>()
        .expect("Could not parse packets into a number");
    println!("Objects {packets}");
    let temp_dir = tempfile::tempdir().expect("Unable to create temp dir");
    let temp_dir2 = tempfile::tempdir().expect("Unable to create temp dir");

    for _ in 0..packets {
        let packet = match deserialize_from_stream(&mut conn.stream) {
            Ok(data) => data,
            Err(e) => panic!("{e}"),
        };

        MerkleTree::write_packet(temp_dir.path().into(), packet).expect("Unable to write packet");
    }

    th.join().expect("Failed to join thread");
    let node =
        MerkleTree::from(temp_dir, temp_dir2.path().to_path_buf()).expect("Unable to create tree");
    let node = match node {
        Node::Tree(tree) => tree,
        Node::Leaf(_) => panic!("Not a tree"),
    };

    assert_eq!(node.get_hash(), tree.get_hash());
}
