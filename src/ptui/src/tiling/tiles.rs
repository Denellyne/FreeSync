use crate::modifiers::TextModifier;
use crate::ptui::Ptui;
use crate::tiling::pane::Pane;
use crate::tiling::traits::Printable;
use crate::traits::{TerminalManager, TextManager};
use std::io;
use std::io::Write;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

pub enum PaneModifier {
    VerticalSplit,
    HorizontalSplit,
    Temporary,
}

pub struct ProgressBar {
    ui: (char, char, char),
    current: Arc<AtomicUsize>,
    total: usize,
    resolution: f32,
    text: Option<(Line, usize)>,
}

pub struct Line {
    string: String,
    modifier: Option<TextModifier>,
    accents_len: u16,
}

pub struct Temporary {
    tile: Box<Tile>,
}

pub enum Tile {
    Line(Line),
    ProgressBar(ProgressBar),
    Pane(Pane),
    Temporary(Temporary),
}

impl Line {
    pub fn new(string: String, modifier: Option<TextModifier>, num_accents: u16) -> Self {
        let accents_len = (Ptui::get_accents().len() * 2) * num_accents as usize;
        Line {
            string,
            modifier,
            accents_len: accents_len as u16,
        }
    }
    pub fn set_string(&mut self, text: String, accents_len: u16) {
        self.string = text;
        self.accents_len = accents_len;
    }
}
impl ProgressBar {
    pub fn new(
        current: Arc<AtomicUsize>,
        total: usize,
        resolution: usize,
        only_bar: bool,
    ) -> ProgressBar {
        let text = Self::generate_text(Arc::clone(&current), total, only_bar);
        let resolution = resolution as f32 / Ptui::get_terminal_size().0 as f32;

        ProgressBar {
            ui: ('=', '<', '>'),
            current,
            total,
            resolution,
            text,
        }
    }
    pub fn new_ex(
        ui: (char, char, char),
        current: Arc<AtomicUsize>,
        total: usize,
        resolution: usize,
        only_bar: bool,
    ) -> ProgressBar {
        let text = Self::generate_text(Arc::clone(&current), total, only_bar);
        let resolution = resolution as f32 / Ptui::get_terminal_size().0 as f32;

        ProgressBar {
            ui,
            current,
            total,
            resolution,
            text,
        }
    }

    fn generate_text(
        current: Arc<AtomicUsize>,
        total: usize,
        only_bar: bool,
    ) -> Option<(Line, usize)> {
        if !only_bar {
            let accented_str = Ptui::color_string("Progress:", &Ptui::get_accents());
            let length = accented_str.len();
            let str = format!(
                "{} {} objects of {}",
                accented_str,
                current.load(Ordering::SeqCst),
                total
            );
            Some((Line::new(str, None, 1), length))
        } else {
            None
        }
    }

    pub fn incr(&mut self) {
        self.current.fetch_add(1, Ordering::SeqCst);
    }

    fn progress_bar(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        let text = &mut self.text.as_mut().unwrap().0;

        let row = text.print(pos, dimensions);
        self.progress_bar_simple((row,pos.1), dimensions)
    }

    fn progress_bar_simple(&self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        Ptui::set_cursor(pos);

        let resolution = self.resolution * dimensions.0 as f32;
        let resolution = resolution as usize;

        let progress_bar_percent = resolution * self.current.load(Ordering::SeqCst) / self.total;

        let (ch, ldelim, rdelim) = self.ui;
        print!(
            "{ldelim}{}{}{rdelim} {}%",
            ch.to_string().repeat(progress_bar_percent),
            " ".repeat(resolution - progress_bar_percent),
            progress_bar_percent * 100 / resolution
        );
        pos.1 + 2
    }
}
impl Temporary {
    pub fn create(tile: Tile) -> Tile {
        Tile::Temporary(Temporary {
            tile: Box::from(tile),
        })
    }
}
impl TerminalManager for Pane {}
impl TextManager for Line {}
impl Printable for Line {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        let (mut rows, cols) = pos;
        let (mut height,width) = dimensions;
        let mut slice = self.string.as_str();
        let modify = match &self.modifier {
            Some(modifier) => {
                print!("{}", TextModifier::get(modifier));
                true
            }
            None => false,
        };
        let mut length = slice.len() - 2*self.accents_len as usize;

        while length >= width {
            if width == 0 || height == 0 {
                break;
            }
            Pane::set_cursor((rows, cols));
            let str = &slice[..width];

            print!("{str}");
            rows += 2;
            height -= 2;
            length -= width;
            slice = &slice[width..];
        }
        if modify {
            Self::reset_foreground();
            Self::reset_background();
            io::stdout().flush().unwrap();
        }
        if !slice.is_empty() {
            print!("{slice}");
            rows += 2;
        }

        rows
    }
}
impl Printable for ProgressBar {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        Ptui::set_cursor(pos);

        match self.text {
            Some(_) => self.progress_bar(pos, dimensions),
            None => self.progress_bar_simple(pos, dimensions),
        }
    }
}
impl Printable for Temporary {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        self.tile.print(pos, dimensions)
    }
}
impl Printable for Tile {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        match self {
            Tile::Line(line) => line.print(pos, dimensions),
            Tile::ProgressBar(progress_bar) => progress_bar.print(pos, dimensions),
            Tile::Pane(pane) => pane.print(pos, dimensions),
            Tile::Temporary(temporary) => temporary.print(pos, dimensions),
        }
    }
}
