use std::sync::atomic::{AtomicUsize, Ordering};
use std::{
    panic::{self, AssertUnwindSafe},
    sync::{Arc, Mutex, mpsc},
    thread::{self, sleep},
    time::Duration,
};

pub struct ThreadPool {
    workers: Vec<Worker>,
    sender: Option<mpsc::Sender<Job>>,
    active_jobs: Arc<AtomicUsize>,
}

type Job = Box<dyn FnOnce() + Send + 'static>;

impl ThreadPool {
    /// Create a new ThreadPool.
    ///
    /// The size is the number of threads in the pool.
    ///
    /// # Panics
    ///
    /// The `new` function will panic if the size is zero.
    pub fn new(size: usize) -> ThreadPool {
        assert!(size > 0);

        let (sender, receiver) = mpsc::channel();

        let receiver = Arc::new(Mutex::new(receiver));
        let active_jobs = Arc::new(AtomicUsize::new(0));

        let mut workers = Vec::with_capacity(size);

        for id in 0..size {
            workers.push(Worker::new(
                id,
                Arc::clone(&receiver),
                Arc::clone(&active_jobs),
            ));
        }

        ThreadPool {
            workers,
            sender: Some(sender),
            active_jobs,
        }
    }

    pub fn execute<F>(&self, f: F)
    where
        F: FnOnce() + Send + 'static,
    {
        let job = Box::new(move || {
            f();
        });

        if let Some(sender) = self.sender.as_ref() {
            self.active_jobs.fetch_add(1, Ordering::SeqCst);
            if sender.send(job).is_err() {
                self.active_jobs.fetch_sub(1, Ordering::SeqCst);
            }
        }
    }

    /// Timeout is in milliseconds
    pub fn join_with_timeout(&mut self, timeout: u64) {
        drop(self.sender.take());
        sleep(Duration::from_millis(timeout));

        for worker in &mut self.workers {
            println!("Shutting down worker {}", worker.id);

            if let Some(thread) = worker.thread.take() {
                drop(thread);
            }
        }
    }
    pub fn join_all(&self) {
        while self.active_jobs.load(Ordering::SeqCst) > 0 {
            sleep(Duration::from_millis(1000));
        }
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        drop(self.sender.take());

        for worker in &mut self.workers {
            if let Some(thread) = worker.thread.take() {
                thread.join().expect("Unable to join thread");
            }
        }
    }
}

struct Worker {
    id: usize,
    thread: Option<thread::JoinHandle<()>>,
}

impl Worker {
    fn new(
        id: usize,
        receiver: Arc<Mutex<mpsc::Receiver<Job>>>,
        running: Arc<AtomicUsize>,
    ) -> Worker {
        let thread = thread::spawn(move || {
            loop {
                let message = receiver.lock().expect("Unable to lock receiver").recv();

                if let Ok(job) = message {
                    #[cfg(debug_assertions)]
                    println!("Worker {id} got a job; executing.");

                    let result = panic::catch_unwind(AssertUnwindSafe(job));
                    if let Err(e) = result {
                        println!("Worker {id}: job panicked but thread survived. {:?}", e);
                    }
                    running.fetch_sub(1, Ordering::SeqCst);
                } else {
                    return;
                }
            }
        });

        Worker {
            id,
            thread: Some(thread),
        }
    }
}
