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

        let mut workers = Vec::with_capacity(size + 2);

        for id in 0..size {
            workers.push(Worker::new(id, Arc::clone(&receiver)));
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
        let active_jobs = Arc::clone(&self.active_jobs);
        let job = Box::new(move || {
            f();
            active_jobs.fetch_sub(1, Ordering::SeqCst);
        });
        let active_jobs = Arc::clone(&self.active_jobs);
        active_jobs.fetch_add(1, Ordering::SeqCst);

        if let Some(sender) = self.sender.as_ref() {
            if sender.send(job).is_err() {
                active_jobs.fetch_sub(1, Ordering::SeqCst);
            }
        } else if self.sender.as_ref().is_none() {
            active_jobs.fetch_sub(1, Ordering::SeqCst);
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
            println!("Shutting down worker {}", worker.id);

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
    fn new(id: usize, receiver: Arc<Mutex<mpsc::Receiver<Job>>>) -> Worker {
        let thread = thread::spawn(move || {
            loop {
                let message = receiver.lock().expect("Unable to lock receiver").recv();

                match message {
                    Ok(job) => {
                        #[cfg(debug_assertions)]
                        println!("Worker {id} got a job; executing.");

                        let result = panic::catch_unwind(AssertUnwindSafe(job));
                        if result.is_err() {
                            println!("Worker {id}: job panicked but thread survived.");
                        }
                    }
                    Err(_) => {
                        println!("Worker {id} disconnected; shutting down.");
                        break;
                    }
                }
            }
        });

        Worker {
            id,
            thread: Some(thread),
        }
    }
}
