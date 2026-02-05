use crate::server_internals::server::Server;
use rand::random;
use std::net::TcpStream;

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
    let server = Server::new("25565".parse().unwrap());
    server.run_server();
    let mut stream = TcpStream::connect(format!("127.0.0.1:{}", 25565)).unwrap();
}

