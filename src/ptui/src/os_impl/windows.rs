use crate::ptui::Ptui;
use std::io::stdout;
use std::sync::atomic::Ordering::Relaxed;
use windows_sys::core::BOOL;

pub(crate) trait TerminalManagerImpl {
    fn get_terminal_size() -> (u16, u16) {
        use winapi_util::console::*;
        let handle = stdout();
        let terminal_info = screen_buffer_info(handle).unwrap();

        let (x, y) = terminal_info.size();
        (x as u16, y as u16)
    }
    unsafe extern "system" fn signal_handler(signal: u32) -> BOOL {
        let ctrlc = windows_sys::Win32::System::Console::CTRL_C_EVENT;

        if signal == ctrlc {
            crate::ptui::RUNNING.store(false, Relaxed);
            Ptui::finalize();
            return 1;
        }
        0
    }

    fn init_signal() {
        unsafe {
            windows_sys::Win32::System::Console::SetConsoleCtrlHandler(
                Some(Self::signal_handler),
                1,
            );
        }
    }
}
