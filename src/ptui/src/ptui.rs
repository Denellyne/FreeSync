use crate::modifiers::{BackgroundModifier, ForegroundModifier};
use crate::tiling::pane::Pane;
use crate::tiling::tiles::Tile;
use crate::tiling::traits::Printable;
use crate::traits::{TerminalManager, TextManager};
use std::io;
use std::io::Write;
use std::sync::{Mutex, OnceLock};

static PANE: Mutex<Pane> = Mutex::new(Pane::new(0, 0, (0, 1)));

pub struct Ptui {
    errors: Vec<String>,
    pane: &'static Mutex<Pane>,
    bg: BackgroundModifier,
    accents: ForegroundModifier,
    dimensions: (u16, u16),
}

fn ptui() -> &'static Mutex<Ptui> {
    static PTUI: OnceLock<Mutex<Ptui>> = OnceLock::new();
    PTUI.get_or_init(|| {
        Mutex::new(Ptui {
            errors: vec![],
            pane: &PANE,
            bg: BackgroundModifier::Black,
            accents: ForegroundModifier::White,
            dimensions: (0, 0),
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
        let title = Self::color_string(&title, &fg);
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
    pub fn push(tile: Tile) -> usize {
        PANE.lock().unwrap().push(tile)
    }

    fn render_loop(&mut self) {
        let (rows, cols) = Self::get_terminal_size();
        if (rows, cols) != self.dimensions {
            Self::clear_screen();
            self.dimensions = (rows, cols);
        }

        let mut pane = self.pane.lock().expect("Unable to lock pane");
        pane.print((0, 3), (rows as usize, cols as usize - 3));
        io::stdout().flush().unwrap();
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
