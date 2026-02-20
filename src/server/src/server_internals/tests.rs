use crate::server_internals::server::Server;
use rand::random;
use std::io::Write;
use std::net::{Shutdown, TcpStream};
use std::thread;

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
    fn write(&mut self) {
        self.stream
            .write_all(format!("{}\n", self.data).as_bytes())
            .expect("Failed to write to stream");
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
    let th = thread::spawn(|| Server::new("25565".parse().unwrap()).mock_server());

    let mut conn = MockConnection::new();
    conn.write();
    conn.close();

    let result = th.join().expect("Failed to join thread");

    assert!(
        conn.get_data()
            .eq(result.first().expect("Failed to get first data"))
    );
}
