use crate::traits::{TerminalManager, TextManager};
use std::fmt;

pub struct Ptui {}

impl Ptui {
    /*
    Call this function inside main before anything else
    */
    pub fn init() -> Ptui {
        // Enter alternate mode
        print!("\x1B[?1049h");

        // Hide cursor
        print!("\x1B[?25l");
        Self::clear_screen();
        Ptui {}
    }
    pub fn play_sound() {
        print!("\x07");
    }

    pub fn print(args: fmt::Arguments) {
        print!("{}", args);
    }

    pub fn println(args: fmt::Arguments) {
        println!("{}", args);
    }
}

impl TerminalManager for Ptui {}
impl TextManager for Ptui {}

impl Drop for Ptui {
    fn drop(&mut self) {
        //  Exit alternate mode
        print!("\x1B[?1049l");
    }
}

#[macro_export]
macro_rules! ptui_println {
    ($($arg:tt)*) => {
        Ptui::println(format_args!($($arg)*));
    };
}
#[macro_export]
macro_rules! ptui_print {
    ($($arg:tt)*) => {
        Ptui::print(format_args!($($arg)*));
    };
}
