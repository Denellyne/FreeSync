pub(crate) mod args;
mod client;
use args::parse_args;
use ptui::modifiers::BackgroundModifier;
use ptui::ptui::Ptui;
use ptui::traits::{TerminalManager, TextManager};
use std::env;

fn main() {
    let _ptui = Ptui::init();
    Ptui::set_background(BackgroundModifier::Custom("\x1b[48;5;16m".to_string()));
    parse_args(env::args().collect());
    Ptui::wait_input()
}
