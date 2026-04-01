use crate::traits::{TerminalManager, TextManager};
use std::io::Write;
use std::sync::{Mutex, OnceLock};
use std::{ io};
use crate::tiling::Pane;


pub struct Ptui {
    errors: Vec<String>,
    panes: Vec<Pane>,
    title: String,
}

fn ptui() -> &'static Mutex<Ptui> {
    static PTUI: OnceLock<Mutex<Ptui>> = OnceLock::new();
    PTUI.get_or_init(|| {
        Mutex::new(Ptui {
            errors: vec![],
            panes: vec![],
            title: "".to_string(),
        })
    })
}

impl Ptui {
    pub fn init(title : String) {
        // Enter alternate screen and hide cursor
        print!("\x1B[?1049h\x1B[?25l");
        io::stdout().flush().unwrap();
        Self::clear_screen();
        let ptui = ptui();
        ptui.lock().unwrap().title = title;
    }

    // pub fn push(args: fmt::Arguments) {
    //     ptui().lock().unwrap().buffer.push(format!("{}", args));
    // }
    // 
    // pub fn pushln(args: fmt::Arguments) {
    //     ptui().lock().unwrap().buffer.push(format!("{}\n", args));
    // }
    // 
    // pub fn eprintln(args: fmt::Arguments) {
    //     ptui().lock().unwrap().errors.push(
    //         Self::color_string(format_args!("{args}").to_string(), ForegroundModifier::Red)
    //             .to_string(),
    //     );
    // }
    // 
    // pub fn play_sound() {
    //     ptui().lock().unwrap().buffer.push("\x07".to_string());
    // }
    // 
    // pub fn print() {
    //     let mut ptui = ptui().lock().unwrap();
    //     for s in ptui.buffer.iter() {
    //         print!("{}", s);
    //     }
    //     ptui.buffer.clear();
    //     io::stdout().flush().unwrap();
    // }

    pub fn render(&self){
        Self::clear_screen();
        let (x,y) = Self::get_terminal_size();
        for pane in self.panes.iter() {
            Self::reset_cursor();
            pane.print()

        }
    }
}
impl TerminalManager for Ptui {}
impl TextManager for Ptui {}

impl Drop for Ptui {
    fn drop(&mut self) {
        Self::clear_screen();
        for error in self.errors.iter() {
            println!("{}", error);
        }
        print!("\x1B[?1049l"); // exit alternate screen
        io::stdout().flush().unwrap();
    }
}

#[macro_export]
macro_rules! ptui_pushln { ($($arg:tt)*) => { Ptui::pushln(format_args!($($arg)*)) }; }
#[macro_export]
macro_rules! ptui_push { ($($arg:tt)*) => { Ptui::push(format_args!($($arg)*)); };}
