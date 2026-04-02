use crate::modifiers::ForegroundModifier::White;
use crate::modifiers::{BackgroundModifier, ForegroundModifier, TextModifier};
use crate::os_impl::TerminalManagerImpl;
use crate::ptui::Ptui;
use std::io::{Read, Write, stdin, stdout};

pub trait TerminalManager: TerminalManagerImpl {
    fn clear_screen()
    where
        Self: Sized,
    {
        print!("\x1B[2J\x1B[1;1H");

        stdout().flush().unwrap();
    }
    fn reset_cursor()
    where
        Self: Sized,
    {
        print!("\x1B[H");
        stdout().flush().unwrap();
    }
    fn clear_line() -> String
    where
        Self: Sized,
    {
        "\x1B[1A\x1B[K".to_string()
    }
    fn set_cursor(pos: (usize, usize))
    where
        Self: Sized,
    {
        print!("\x1B[{};{}f", pos.0, pos.1)
    }
}

pub trait TextManager {
    fn color_string(text: &str, modifier: &ForegroundModifier) -> String {
        let modifier = TextModifier::get_foreground_modifier(modifier);
        let default = TextModifier::get_foreground_modifier(&White);
        format!("{modifier}{text}{default}")
    }

    fn color_string_ex(
        text: String,
        modifier: ForegroundModifier,
        default: ForegroundModifier,
    ) -> String {
        let modifier = TextModifier::get_foreground_modifier(&modifier);
        let default = TextModifier::get_foreground_modifier(&default);
        format!("{modifier}{text}{default}")
    }

    fn wait_input() {
        // ptui_pushln!("Press any key to exit");
        let _ = stdin().read(&mut [0u8]).unwrap();
    }

    fn set_foreground(foreground: ForegroundModifier) {
        print!("{}", TextModifier::get_foreground_modifier(&foreground));
    }

    fn reset_foreground() {
        print!("{}", TextModifier::get_foreground_modifier(&White));
    }
    fn reset_background() {
        print!("{}", TextModifier::get_background_modifier(&Ptui::get_bg()));
    }
    fn set_background(background: BackgroundModifier) {
        print!("{}", TextModifier::get_background_modifier(&background));
    }
}
