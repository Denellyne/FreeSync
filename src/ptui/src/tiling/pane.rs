use crate::os_impl::TerminalManagerImpl;
use crate::tiling::tiles::Tile;
use crate::tiling::traits::Printable;
use crate::traits::TerminalManager;
use std::sync::Mutex;

pub struct Pane {
    width: usize,
    height: usize,
    pos: (u32, u32),
    tiles: Vec<Mutex<Tile>>,
    title: String,
}

impl Pane {
    pub const fn new(width: usize, height: usize, pos: (u32, u32)) -> Self {
        Pane {
            width,
            height,
            pos,
            tiles: vec![],
            title: String::new(),
        }
    }

    fn print_title(&self, width: usize) {
        let str_len = self.title.len().saturating_sub(16);
        let pos = width.saturating_sub(str_len) >> 1;
        Self::set_cursor((0, pos as u32));
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
    pub(crate) fn set_pos(&mut self, pos: (u32, u32)) {
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
impl Printable for Pane {
    fn print(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        self.set_dimensions(dimensions);
        self.set_pos(pos);
        self.print_title(self.width);
        let mut last_row = self.pos.0 + 1;
        for tile in self.tiles.iter() {
            let pos = (last_row, self.pos.1);
            Self::set_cursor(pos);

            let dimensions = (self.height - 1, self.width);
            last_row = match &mut *tile.lock().expect("Unable to lock tile") {
                Tile::Line(line) => line.print(pos, dimensions),
                Tile::ProgressBar(progress_bar) => progress_bar.print(pos, dimensions),
                Tile::Pane(pane) => pane.print(pos, dimensions),
                Tile::Temporary(tile) => tile.print(pos, dimensions),
            } + 1;
        }
        let len = self.tiles.len();
        self.tiles
            .retain(|tile| !matches!(*tile.lock().unwrap(), Tile::Temporary(_)));
        if self.tiles.len() != len {
            Self::clear_screen();
        }

        last_row
    }
}
impl TerminalManager for Pane {}
impl TerminalManagerImpl for Pane {}
