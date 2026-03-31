use crate::modifiers::ForegroundModifier::White;
use crate::modifiers::{BackgroundModifier, ForegroundModifier, TextModifier};
use crate::ptui::Ptui;
use crate::ptui_println;
use std::io::{Read, stdin};

pub trait TerminalManager {
    fn clear_screen() {
        print!("\x1B[2J\x1B[1;1H");
    }
    fn clear_line() -> String {
        "\x1B[1A\x1B[K".to_string()
    }
    fn set_background(background: BackgroundModifier) {
        print!("{}", TextModifier::get_background_modifier(background));
        Self::clear_screen()
    }

    fn set_foreground(foreground: ForegroundModifier) {
        print!("{}", TextModifier::get_foreground_modifier(foreground));
    }

    fn reset_foreground() {
        print!(
            "{}",
            TextModifier::get_foreground_modifier(ForegroundModifier::White)
        );
    }
}
pub trait TextManager {
    fn color_string(text: String, modifier: ForegroundModifier) -> String {
        let modifier = TextModifier::get_foreground_modifier(modifier);
        let default = TextModifier::get_foreground_modifier(White);
        format!("{modifier}{text}{default}")
    }

    fn color_string_ex(
        text: String,
        modifier: ForegroundModifier,
        default: ForegroundModifier,
    ) -> String {
        let modifier = TextModifier::get_foreground_modifier(modifier);
        let default = TextModifier::get_foreground_modifier(default);
        format!("{modifier}{text}{default}")
    }

    fn wait_input() {
        ptui_println!("Press any key to exit");
        let _ = stdin().read(&mut [0u8]).unwrap();
    }

    fn progress_bar(
        ui: (char, char, char),
        resolution: usize,
        current: usize,
        total: usize,
    ) -> String {
        let progress_bar_percent = resolution * current / total;
        let ldelim: char;
        let rdelim: char;
        let ch: char;
        (ch, ldelim, rdelim) = ui;
        format!(
            "{ldelim}{}{}{rdelim} {}%",
            ch.to_string().repeat(progress_bar_percent),
            " ".repeat(resolution - progress_bar_percent),
            progress_bar_percent * 100 / resolution
        )
    }
}
