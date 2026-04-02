use std::sync::atomic::{AtomicUsize, Ordering};

use crate::modifiers::{ForegroundModifier, TextModifier};
use crate::tiling::pane::Pane;
use crate::tiling::traits::Printable;
use crate::traits::{TerminalManager, TextManager};

enum Line {
    Custom(String, ForegroundModifier),
    Plain(String),
    Dynamic((String, String), AtomicUsize, Ordering),
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
                lines.push(Line::Plain(string.drain(..val).collect()));
            }
            let modifier: String = string
                .drain(..=string.find("m").expect("Malformed Opening Modifier"))
                .collect();
            let str: String = string
                .drain(..string.find("\x1B[").expect("Malformed Closing Modifier"))
                .collect();
            lines.push(Line::Custom(str, ForegroundModifier::Custom(modifier)));
            let _: String = string
                .drain(..=string.find("m").expect("Malformed Closing Modifier"))
                .collect();
        }

        if !string.is_empty() {
            lines.push(Line::Plain(string));
        }
        lines
    }

    fn print_modifier(&self, modifier: &ForegroundModifier) {
        print!("{}", TextModifier::get_foreground_modifier(modifier));
    }
}

impl Printable for TextTile {
    fn print(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32 {
        let (mut rows, cols) = pos;
        let (height, width) = dimensions;
        let mut buf = String::with_capacity(128);

        for line in &self.lines {
            if width == 0 || height.saturating_sub(rows as usize) == 0 {
                break;
            }
            let is_custom = match line {
                Line::Custom(slice, foreground) => {
                    self.print_modifier(foreground);
                    buf.clear();
                    buf.push_str(slice);
                    true
                }
                Line::Plain(slice) => {
                    buf.clear();
                    buf.push_str(slice);
                    false
                }
                Line::Dynamic((pre, sub), atomic, ord) => {
                    buf = format!("{}{}{}", pre, atomic.load(*ord), sub);
                    false
                }
            };
            let mut slice = buf.as_str();

            let mut length = slice.len();

            while length >= width {
                Pane::set_cursor((rows, cols));
                let str = &slice[..width];

                print!("{str}");
                rows += 1;
                length -= width;
                slice = &slice[width..];
            }

            if !slice.is_empty() {
                print!("{slice}");
            }
            if is_custom {
                print! {"{}",TextModifier::get_foreground_modifier(&ForegroundModifier::White)}
            }
        }

        rows
    }
}
impl TextManager for TextTile {}
