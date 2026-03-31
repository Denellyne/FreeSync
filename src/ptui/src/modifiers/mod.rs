#[derive(Debug, Clone)]

pub enum ForegroundModifier {
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    Custom(String),
}
#[derive(Debug, Clone)]
pub enum BackgroundModifier {
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    Custom(String),
}

#[derive(Debug, Clone)]

pub enum TextModifier {
    Foreground(ForegroundModifier),
    Background(BackgroundModifier),
}

impl TextModifier {
    pub fn get_background_modifier(modifier: BackgroundModifier) -> String {
        match modifier {
            BackgroundModifier::Black => "\x1B[40m",
            BackgroundModifier::Red => "\x1B[41m",
            BackgroundModifier::Green => "\x1B[42m",
            BackgroundModifier::Yellow => "\x1B[43m",
            BackgroundModifier::Blue => "\x1B[44m",
            BackgroundModifier::Magenta => "\x1B[45m",
            BackgroundModifier::Cyan => "\x1B[46m",
            BackgroundModifier::White => "\x1B[47m",
            BackgroundModifier::Custom(s) => return s,
        }
        .to_string()
    }
    pub fn get_foreground_modifier(modifier: ForegroundModifier) -> String {
        match modifier {
            ForegroundModifier::Black => "\x1B[30m",
            ForegroundModifier::Red => "\x1B[31m",
            ForegroundModifier::Green => "\x1B[32m",
            ForegroundModifier::Yellow => "\x1B[33m",
            ForegroundModifier::Blue => "\x1B[34m",
            ForegroundModifier::Magenta => "\x1B[35m",
            ForegroundModifier::Cyan => "\x1B[36m",
            ForegroundModifier::White => "\x1B[37m",
            ForegroundModifier::Custom(s) => return s,
        }
        .to_string()
    }
}
