use crate::server_internals::server::Server;
use logger::mock::MockLogger;
use merkle::merklenode::node::Node;
use rand::random;
use std::io::{BufReader, Read, Write};
use std::net::{Shutdown, TcpStream};
use std::thread::{self};

struct MockConnection {
    stream: TcpStream,
    data: String,
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

#[test]
fn test_connection() {
    let tx = MockLogger::create();
    let sv = Server::new("25565".parse().unwrap(), tx);
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
    let sv = Server::new("25567".parse().unwrap(), tx);
    let th = thread::spawn(move || sv.mock_server());

    let mut conn = MockConnection::from("TEST".to_string(), 25567);
    conn.write();
    let data = conn.read();
    conn.close();

    let _result = th.join().expect("Failed to join thread");
    println!("{}", String::from_utf8_lossy(&data));

    assert!(data == "OK".as_bytes());
}

#[test]
fn test_clone() {
    let tx = MockLogger::create();
    let sv = Server::new("25566".parse().unwrap(), tx);
    let node1 = sv.tree.clone();
    let th = thread::spawn(move || sv.mock_server());

    let mut conn = MockConnection::from("CLONE".to_string(), 25566);
    conn.write();
    let data = conn.read();
    let node: Node = bincode::deserialize(&data).unwrap();
    println!("{:?}", node);
    conn.close();

    th.join().expect("Failed to join thread");

    assert!(node.eq(&node1));
}
