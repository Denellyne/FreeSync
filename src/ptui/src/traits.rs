use crate::modifiers::ForegroundModifier::White;
use crate::modifiers::{BackgroundModifier, ForegroundModifier, TextModifier};
use crate::ptui::Ptui;
use std::io::{Read, Write, stdin, stdout};

pub trait TerminalManager {
    fn clear_screen() {
        print!("\x1B[2J\x1B[1;1H");

        stdout().flush().unwrap();
    }
    fn reset_cursor() {
        print!("\x1B[H");
        stdout().flush().unwrap();
    }
    fn clear_line() -> String {
        "\x1B[1A\x1B[K".to_string()
    }
    fn set_cursor(pos: (usize, usize)) {
        print!("\x1B[{};{}f", pos.0, pos.1)
    }

    #[cfg(windows)]
    fn get_terminal_size() -> (u16, u16) {
        use winapi_util::console::*;
        let handle = stdout();
        let terminal_info = screen_buffer_info(handle).unwrap();

        let (x, y) = terminal_info.size();
        (x as u16, y as u16)
    }

    #[cfg(unix)]
    fn get_terminal_size() -> (u16, u16) {
        unsafe {
            use std::os::fd::AsRawFd;

            use nix::libc::{self};
            let mut win: libc::winsize = libc::winsize {
                ws_row: 0,
                ws_col: 0,
                ws_xpixel: 0,
                ws_ypixel: 0,
            };
            libc::ioctl(stdout().as_raw_fd(), libc::TIOCGWINSZ, &mut win);

            (win.ws_col + 1, win.ws_row + 1)
        }
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
    fn progress_bar(current: usize, total: usize, modifier: ForegroundModifier) {
        let str = format!(
            "{}{} {} objects of {total}",
            Ptui::clear_line().repeat(2),
            Ptui::color_string("Progress:", &modifier),
            current
        );

        // ptui_pushln!(
        //             "{str}\n{}",
        //             Ptui::progress_bar_simple(
        //                 current,
        //                 total
        //             )
        //         );
    }

    fn progress_bar_simple(current: usize, total: usize) -> String {
        Self::progress_bar_simple_ex(('=', '<', '>'), 32, current, total)
    }

    fn progress_bar_simple_ex(
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

pub trait Printable {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize;
}
