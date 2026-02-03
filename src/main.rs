pub(crate) mod args;
mod merkle;
mod server;

use args::parse_args;
use std::env;

fn main() {
    #[cfg(not(feature = "server"))]
    parse_args(env::args().collect());
    #[cfg(feature = "server")]
    println!("Starting server");
}
/*
Todo
Create Server
allow symbolic links or not
*/
