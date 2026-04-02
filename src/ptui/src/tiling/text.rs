use crate::modifiers::{ForegroundModifier, TextModifier};
use crate::tiling::pane::Pane;
use crate::tiling::traits::Printable;
use crate::traits::{TerminalManager, TextManager};

enum Line {
    CLine(String, ForegroundModifier),
    PLine(String),
}

pub struct TextTile {
    lines: Vec<Line>,
}
impl TextTile {
    pub fn new(string: String) -> Self {
        TextTile {
            lines: Self::convert_string(string),
        }
    }
    pub fn set_string(&mut self, text: String) {
        self.lines = Self::convert_string(text);
    }

    fn convert_string(mut string: String) -> Vec<Line> {
        let mut lines: Vec<Line> = vec![];

        while let Some(val) = string.find("\x1B[") {
            if val > 0 {
                lines.push(Line::PLine(string.drain(..val).collect()));
            }
            let modifier: String = string
                .drain(..=string.find("m").expect("Malformed Opening Modifier"))
                .collect();
            let str: String = string
                .drain(..string.find("\x1B[").expect("Malformed Closing Modifier"))
                .collect();
            lines.push(Line::CLine(str, ForegroundModifier::Custom(modifier)));
            let _: String = string
                .drain(..=string.find("m").expect("Malformed Closing Modifier"))
                .collect();
        }

        if !string.is_empty() {
            lines.push(Line::PLine(string));
        }
        vec![]
    }

    fn print_modifier(&self, modifier: &ForegroundModifier) {
        print!("{}", TextModifier::get_foreground_modifier(modifier));
    }
}

impl Printable for TextTile {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize {
        let (mut rows, cols) = pos;
        let (mut height, width) = dimensions;

        for line in &self.lines {
            let mut slice = match line {
                Line::CLine(slice, foreground) => {
                    self.print_modifier(&foreground);
                    slice
                }
                Line::PLine(slice) => slice,
            }
            .as_str();

            let mut length = slice.len();

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

            if !slice.is_empty() {
                print!("{slice}");
                rows += 2;
            }
        }

        rows
    }
}
impl TextManager for TextTile {}
