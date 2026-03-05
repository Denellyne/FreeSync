pub(crate) mod args;

use args::parse_args;
use std::env;

fn main() {
    parse_args(env::args().collect());
}
