#[cfg(unix)]
pub(super) mod unix;

#[cfg(unix)]
#[allow(unused)]
pub use unix::*;

#[cfg(windows)]
pub(super) mod windows;

#[cfg(windows)]
#[allow(unused)]
pub use windows::*;
