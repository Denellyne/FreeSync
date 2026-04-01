pub trait Printable {
    fn print(&mut self, pos: (usize, usize), dimensions: (usize, usize)) -> usize;
}
