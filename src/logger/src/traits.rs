pub trait Log {
    fn write(&mut self, data: String);
    fn log_rcv(&mut self);
}
