pub(crate) mod args;
mod client;
use args::parse_args;
use ptui::modifiers::{BackgroundModifier, ForegroundModifier};
use ptui::ptui::Ptui;
use ptui::traits::{TerminalManager, TextManager};
use std::env;

fn main() {
    Ptui::init("FreeSync".to_string(),);
    Ptui::set_background(BackgroundModifier::Custom("\x1b[48;5;16m".to_string()));
    // ptui_println!(
    //         "{}",
    //         Ptui::color_string("FreeSync:".to_string(),ForegroundModifier::Custom("\x1b[38;5;61m".to_string()))
    //     );
    parse_args(env::args().collect());
    Ptui::wait_input()
}
