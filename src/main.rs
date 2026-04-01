pub(crate) mod args;
mod client;
use args::parse_args;
use ptui::modifiers::{BackgroundModifier, ForegroundModifier};
use ptui::ptui::Ptui;
use std::time::Duration;
use std::{env, thread};

fn main() {
    Ptui::init(
        "FreeSync".to_string(),
        BackgroundModifier::Custom("\x1b[48;5;16m".to_string()),
        ForegroundModifier::Custom("\x1b[38;5;61m".to_string()),
    );
    let _th = thread::spawn(move || {
        loop {
            Ptui::render();
            thread::sleep(Duration::from_millis(100));
        }
    });
    parse_args(env::args().collect());
}
