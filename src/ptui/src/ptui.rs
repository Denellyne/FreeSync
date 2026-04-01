use crate::modifiers::{BackgroundModifier, ForegroundModifier};
use crate::tiling::Pane;
use crate::traits::{Printable, TerminalManager, TextManager};
use std::io;
use std::io::Write;
use std::sync::{Mutex, OnceLock};
static PANE: Mutex<Pane> = Mutex::new(Pane::new(0, 0, (0, 1)));

pub struct Ptui {
    errors: Vec<String>,
    pane: &'static Mutex<Pane>,
    bg: BackgroundModifier,
    accents: ForegroundModifier,
}

fn ptui() -> &'static Mutex<Ptui> {
    static PTUI: OnceLock<Mutex<Ptui>> = OnceLock::new();
    PTUI.get_or_init(|| {
        Mutex::new(Ptui {
            errors: vec![],
            pane: &PANE,
            bg: BackgroundModifier::Black,
            accents: ForegroundModifier::White,
        })
    })
}

impl Ptui {
    pub fn init(title: String, bg: BackgroundModifier, fg: ForegroundModifier) {
        // Enter alternate screen and hide cursor
        print!("\x1B[?1049h\x1B[?25l");
        io::stdout().flush().unwrap();
        Self::clear_screen();
        let mut ptui = ptui().lock().unwrap();
        ptui.pane.lock().unwrap().set_title(title);
        ptui.bg = bg;
        ptui.accents = fg;
    }
    pub fn get_bg() -> BackgroundModifier {
        ptui().lock().unwrap().bg.clone()
    }
    pub fn get_accents() -> ForegroundModifier {
        ptui().lock().unwrap().accents.clone()
    }

    pub fn get_pane() -> &'static Mutex<Pane> {
        &PANE
    }

    // pub fn new_pane(modifiers: &[PaneModifier]) -> Pane {
    //     let mut vsplit = false;
    //     let mut hsplit = false;
    //     let mut temp = false;
    //     let pane = Pane::new();
    //     pane
    // }

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

    fn render_loop(&mut self) {
        Self::clear_screen();
        let (cols, rows) = Self::get_terminal_size();
        let mut pane = self.pane.lock().expect("Unable to lock pane");
        pane.print((cols as usize, rows as usize), (0, 0));
    }
    pub fn render() {
        ptui().lock().unwrap().render_loop();
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
