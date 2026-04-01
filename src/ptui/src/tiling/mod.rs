use crate::modifiers::TextModifier;
use crate::ptui::Ptui;
use crate::traits::{Printable, TerminalManager, TextManager};
use std::sync::atomic::AtomicUsize;
use std::sync::{Arc, Mutex};
use std::usize;

pub enum PaneModifier {
    VerticalSplit,
    HorizontalSplit,
    Temporary,
}

pub struct ProgressBar {
    ui: (char, char, char),
    current: Arc<AtomicUsize>,
    total: usize,
    resolution: usize,
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
pub struct Pane {
    width: usize,
    height: usize,
    pos: (usize, usize),
    tiles: Vec<Mutex<Tile>>,
    title: String,
}

impl Pane {
    pub const fn new(width: usize, height: usize, pos: (usize, usize)) -> Self {
        Pane {
            width,
            height,
            pos,
            tiles: vec![],
            title: String::new(),
        }
    }
    fn print_title(&self, width: usize) {
        let pos: usize = (width.saturating_sub(self.title.len())) >> 1;
        Self::set_cursor((pos, 0));
        print!("{}", self.title);
    }

    pub fn set_title(&mut self, title: String) {
        self.title = title
    }

    pub fn remove_tile(&mut self, idx: usize) {
        self.tiles.remove(idx);
    }
    pub fn push_tile(&mut self, tile: Tile) -> &Mutex<Tile> {
        self.tiles.push(Mutex::new(tile));
        &self.tiles[self.tiles.len() - 1]
    }
    pub fn get_tile_ref(&self, idx: usize) -> Option<&Mutex<Tile>> {
        if idx >= self.tiles.len() {
            return None;
        }
        Some(&self.tiles[idx])
    }
    pub(crate) fn set_pos(&mut self, pos: (usize, usize)) {
        self.pos = pos
    }
    fn set_dimensions(&mut self, dimensions: (usize, usize)) {
        self.width = dimensions.0;
        self.height = dimensions.1;
    }
    pub fn push(&mut self, tile: Tile) -> usize {
        self.tiles.push(Mutex::new(tile));
        self.tiles.len() - 1
    }
    pub fn insert(&mut self, tile: Tile, idx: usize) -> usize {
        self.tiles.insert(idx, Mutex::new(tile));
        self.tiles.len() - 1
    }
    pub fn get_tile(&mut self, idx: usize) -> Option<(Tile, usize)> {
        if idx >= self.tiles.len() {
            return None;
        }
        let tile = self.tiles.remove(idx).into_inner().unwrap();
        Some((tile, idx))
    }
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
    pub fn new(current: Arc<AtomicUsize>, total: usize, resolution: usize) -> ProgressBar {
        ProgressBar {
            ui: ('=', '<', '<'),
            current,
            total,
            resolution,
        }
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
impl Printable for Pane {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        self.set_dimensions(dimensions);
        self.set_pos(pos);
        self.print_title(self.width);
        let mut last_row = self.pos.1 - 1;
        for tile in self.tiles.iter() {
            Self::set_cursor((self.pos.0, last_row));
            let pos = (self.pos.0, last_row);
            let dimensions = (
                self.width - self.pos.0,
                self.height - (self.pos.1 - last_row),
            );
            last_row = match &mut *tile.lock().expect("Unable to lock tile") {
                Tile::Line(line) => line.print(pos, dimensions),
                Tile::ProgressBar(progress_bar) => progress_bar.print(pos, dimensions),
                Tile::Pane(pane) => pane.print(pos, dimensions),
                Tile::Temporary(tile) => tile.print(pos, dimensions),
            } + 1;
        }
        self.tiles
            .retain(|tile| !matches!(*tile.lock().unwrap(), Tile::Temporary(_)));

        last_row
    }
}
impl Printable for Line {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        let (cols, mut rows) = pos;
        let (width, mut height) = dimensions;
        let mut slice = self.string.as_str();
        let modify = match &self.modifier {
            Some(modifier) => {
                print!("{}", TextModifier::get(modifier));
                true
            }
            None => false,
        };
        let mut length = slice.len() - self.accents_len as usize;

        while length >= width {
            if width == 0 || height == 0 {
                break;
            }
            Pane::set_cursor((cols, rows));
            let str = &slice[..width];

            print!("{str}");
            rows += 1;
            height -= 1;
            length -= width;
            slice = &slice[width..];
        }
        if modify {
            Self::reset_foreground();
            Self::reset_background();
        }

        rows
    }
}
impl Printable for ProgressBar {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        let (cols, rows) = pos;
        let (width, height) = dimensions;
        0
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
