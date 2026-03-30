use logger::mock::MockLogger;
use merkle::merklenode::node::Node;
use merkle::merklenode::traits::TreeIO;
use merkle::merkletree::MerkleTree;
use rand::random;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{Shutdown, TcpStream};
use std::path::{Path, PathBuf};
use std::thread::{self};

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

fn random_data() -> String {
    let mut str: String = String::new();
    let len = random::<u16>() % u16::MAX / 4 + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    str
}
use crate::server::Server;
use merkle::data::deserialize_from_stream;
use merkle::traits::Hashable;
use tempfile::{tempdir_in, NamedTempFile, TempDir};

fn write_random_to_file(file: NamedTempFile) -> (NamedTempFile, String) {
    let mut str: String = String::new();
    let len = random::<u16>() % u16::MAX / 4 + 1;
    for _i in 0..len {
        str.push(random::<char>());
    }
    write!(&file, "{}", str).expect("Unable to write to file");
    (file, str)
}

fn generate_random_file(path: &PathBuf) -> NamedTempFile {
    let (file, _) =
        write_random_to_file(NamedTempFile::new_in(path).expect("Unable to create temporary file"));
    file
}
pub(crate) fn generate_random_tree(
    path: PathBuf,
) -> (Result<Node, String>, Vec<NamedTempFile>, Vec<TempDir>) {
    let size = random::<u8>() % 128 + 1;
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
            let temp_file = generate_random_file(&current_path);
            temporary_files.push(temp_file);
        }
    }

    let tree = MerkleTree::create(path.to_path_buf()).expect("Unable to create tree");
    tree.save_tree().unwrap();
    (Ok(Node::Tree(tree)), temporary_files, temporary_folders)
}

pub(crate) fn random_tree_builder(
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
