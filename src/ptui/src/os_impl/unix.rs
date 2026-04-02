use crate::ptui::Ptui;
use std::io::stdout;
use std::sync::atomic::Ordering::Relaxed;

pub(crate) trait TerminalManagerImpl {
    fn get_terminal_size() -> (u16, u16) {
        unsafe {
            use std::os::fd::AsRawFd;

            use nix::libc::{self};
            let mut win: libc::winsize = libc::winsize {
                ws_row: 0,
                ws_col: 0,
                ws_xpixel: 0,
                ws_ypixel: 0,
            };
            libc::ioctl(stdout().as_raw_fd(), libc::TIOCGWINSZ, &mut win);

            (win.ws_col + 1, win.ws_row + 1)
        }
    }

    unsafe fn signal_handler(signal: u32) -> i32 {
        let ctrlc = nix::sys::signal::Signal::from_c_int(signal).unwrap();

        if signal == ctrlc {
            crate::ptui::RUNNING.store(false, Relaxed);
            Ptui::finalize();
            return 1;
        }
        0
    }
}
