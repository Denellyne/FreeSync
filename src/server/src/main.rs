use logger::Logger;

use crate::server_internals::server::Server;
use std::env;
mod server_internals;

fn main() {
    let mut args: Vec<String> = env::args().collect();
    assert!(args.len() >= 2, "Not enough arguments were given!");

    let port = args.remove(1);

    let tx = Logger::create(
        "./logs/server.log",
        "Server".parse().expect("Unable to parse string"),
        true,
        true,
    );
    let server = Server::new(port, ".", tx);

    server.run_server();
}
