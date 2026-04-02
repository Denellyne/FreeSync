use crate::modifiers::ForegroundModifier::White;
use crate::modifiers::{BackgroundModifier, ForegroundModifier, TextModifier};
use crate::os_impl::TerminalManagerImpl;
use crate::ptui::Ptui;
use crate::tiling::text::TextTile;
use crate::tiling::tiles::Tile;
use std::io::{self, Read, Write, stdin, stdout};

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
    fn set_cursor(pos: (u32, u32))
    where
        Self: Sized,
    {
        print!("\x1B[{};{}H", pos.0 + 1, pos.1 + 1);
        stdout().flush().unwrap();
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
        let _ = Ptui::push(Tile::Line(TextTile::new(
            "Press any key to exit.".to_owned(),
        )));
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
