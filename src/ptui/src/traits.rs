use crate::modifiers::ForegroundModifier::White;
use crate::modifiers::{BackgroundModifier, ForegroundModifier, TextModifier};
use crate::ptui::Ptui;
use crate::ptui_pushln;
use std::io::{stdin, stdout, Read, Write};

pub struct A{}
impl TerminalManager for A{}

pub trait TerminalManager {
    fn clear_screen() {
        print!("\x1B[2J\x1B[1;1H");

        stdout().flush().unwrap();
    }
    fn reset_cursor(){
        print!("\x1B[H");
        stdout().flush().unwrap();
    }
    fn clear_line() -> String {
        "\x1B[1A\x1B[K".to_string()
    }
    fn set_background(background: BackgroundModifier) {
        Self::clear_screen();
        print!("{}", TextModifier::get_background_modifier(background));
        stdout().flush().unwrap();

    }

    fn set_foreground(foreground: ForegroundModifier) {
        print!("{}", TextModifier::get_foreground_modifier(foreground));
    }

    fn reset_foreground() {
        print!(
            "{}",
            TextModifier::get_foreground_modifier(White)
        );
    }
    #[cfg(windows)]
    fn get_terminal_size() -> (u16, u16) {
        use winapi_util::console::*;
        let  handle = stdout();
        let terminal_info = screen_buffer_info(handle).unwrap();

        let (x,y) = terminal_info.size();
        (x as u16, y as u16)

    }

  #[cfg(unix)]
    fn get_terminal_size() -> (u16, u16) {
      use nix::ioctl_read;

      let mut x : u16 = 0;
      let mut y : u16 = 0;
      let mut _z : u64 = 0;
      ioctl_read!(stdout(), libc::TIOCGWINSZ, x, y, _z);
        (x, y)
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
        // ptui_pushln!("Press any key to exit");
        let _ = stdin().read(&mut [0u8]).unwrap();
    }
    fn progress_bar(        current: usize,
                            total: usize,
                    modifier : ForegroundModifier){
        let str = format!(
            "{}{} {} objects of {total}",
            Ptui::clear_line().repeat(2),
            Ptui::color_string("Progress:".to_string(), modifier),
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

    fn progress_bar_simple(
        current: usize,
        total: usize,
    ) -> String {
        Self::progress_bar_simple_ex(('=','<','>'),32,current,total)
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
}
