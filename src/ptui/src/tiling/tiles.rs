use crate::os_impl::TerminalManagerImpl;
use crate::ptui::Ptui;
use crate::tiling::pane::Pane;
use crate::tiling::text::TextTile;
use crate::tiling::traits::Printable;
use crate::traits::{TerminalManager, TextManager};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

pub enum PaneModifier {
    VerticalSplit,
    HorizontalSplit,
    Temporary,
}

pub struct ProgressBarTile {
    ui: (char, char, char),
    current: Arc<AtomicUsize>,
    total: usize,
    resolution: f32,
    text: Option<(TextTile, usize)>,
}

pub struct TemporaryTile {
    tile: Box<Tile>,
}

pub enum Tile {
    Line(TextTile),
    ProgressBar(ProgressBarTile),
    Pane(Pane),
    Temporary(TemporaryTile),
}

impl ProgressBarTile {
    pub fn new(
        current: Arc<AtomicUsize>,
        total: usize,
        resolution: usize,
        only_bar: bool,
    ) -> ProgressBarTile {
        let text = Self::generate_text(Arc::clone(&current), total, only_bar);
        let resolution = resolution as f32 / Ptui::get_terminal_size().0 as f32;

        ProgressBarTile {
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
    ) -> ProgressBarTile {
        let text = Self::generate_text(Arc::clone(&current), total, only_bar);
        let resolution = resolution as f32 / Ptui::get_terminal_size().0 as f32;

        ProgressBarTile {
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
    ) -> Option<(TextTile, usize)> {
        if !only_bar {
            let accented_str = Ptui::color_string("Progress:", &Ptui::get_accents());
            let length = accented_str.len();
            let str = format!(
                "{} {} objects of {}",
                accented_str,
                current.load(Ordering::SeqCst),
                total
            );
            Some((TextTile::new(str), length))
        } else {
            None
        }
    }

    pub fn incr(&mut self) {
        self.current.fetch_add(1, Ordering::SeqCst);
    }

    fn progress_bar(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        let text = &mut self.text.as_mut().unwrap().0;

        let row = text.print(pos, dimensions) + 1;
        self.progress_bar_simple((row, pos.1), dimensions)
    }

    fn progress_bar_simple(&self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        Ptui::set_cursor(pos);

        let resolution = self.resolution * dimensions.1 as f32;
        let resolution = resolution as usize;

        let progress_bar_percent = resolution * self.current.load(Ordering::SeqCst) / self.total;

        let (ch, ldelim, rdelim) = self.ui;
        print!(
            "{ldelim}{}{}{rdelim} {}%",
            ch.to_string().repeat(progress_bar_percent),
            " ".repeat(resolution - progress_bar_percent),
            progress_bar_percent * 100 / resolution
        );
        pos.0
    }
}
impl TemporaryTile {
    pub fn create(tile: Tile) -> Tile {
        Tile::Temporary(TemporaryTile {
            tile: Box::from(tile),
        })
    }
}

impl Printable for ProgressBarTile {
    fn print(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        Ptui::set_cursor(pos);

        match self.text {
            Some(_) => self.progress_bar(pos, dimensions),
            None => self.progress_bar_simple(pos, dimensions),
        }
    }
}
impl Printable for TemporaryTile {
    fn print(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        self.tile.print(pos, dimensions)
    }
}
impl Printable for Tile {
    fn print(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        match self {
            Tile::Line(line) => line.print(pos, dimensions),
            Tile::ProgressBar(progress_bar) => progress_bar.print(pos, dimensions),
            Tile::Pane(pane) => pane.print(pos, dimensions),
            Tile::Temporary(temporary) => temporary.print(pos, dimensions),
        }
    }
}
