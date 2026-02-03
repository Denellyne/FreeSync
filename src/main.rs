pub(crate) mod args;
mod merkle;
use args::parse_args;
use std::env;

fn main() {
    parse_args(env::args().collect());
}
/*
Todo
Create Server
allow symbolic links or not
*/
