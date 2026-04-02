pub(crate) mod args;
mod client;
use args::parse_args;
use ptui::modifiers::{BackgroundModifier, ForegroundModifier};
use ptui::ptui::Ptui;
use ptui::traits::TextManager;
use std::env;

fn main() {
    Ptui::init(
        "FreeSync".to_string(),
        BackgroundModifier::Custom("\x1b[48;5;16m".to_string()),
        ForegroundModifier::Custom("\x1b[38;5;61m".to_string()),
        33,
    );

    parse_args(env::args().collect());
    Ptui::wait_input();
    Ptui::finalize();
}
