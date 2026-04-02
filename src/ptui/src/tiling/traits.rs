pub trait Printable {
    fn print(&mut self, pos: (u32, u32), dimensions: (usize, usize)) -> u32;
}
