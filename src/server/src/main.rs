use crate::server_internals::server::Server;
use std::env;
mod server_internals;

fn main() {
    let mut args: Vec<String> = env::args().collect();
    assert!(args.len() >= 2, "Not enough arguments were given!");

    let port = args.remove(1);

    let server = Server::new(port);

    server.run_server();
}
