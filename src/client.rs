use merkle::data::deserialize_from_stream;
use merkle::merkletree::MerkleTree;
use ptui::modifiers::ForegroundModifier;
use ptui::ptui::Ptui;
use ptui::ptui_println;
use ptui::traits::{TerminalManager, TextManager};
use std::env;
use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use threadpool::pool::ThreadPool;

pub struct Client {
    stream: TcpStream,
}

impl Client {
    fn new() -> Result<(Client, String), String> {
        let dir = match env::current_dir() {
            Ok(dir) => dir,
            Err(e) => return Err(e.to_string()),
        };

        let addr = match MerkleTree::get_upstream(dir) {
            Ok(addr) => addr,
            Err(e) => return Err(e.to_string()),
        };

        ptui_println!("Connecting to {}...", addr);
        let stream = TcpStream::connect(&addr)
            .unwrap_or_else(|_| panic!("Failed to connect to server,Upstream : {addr}"));

        let custom = ForegroundModifier::Custom("\x1b[38;5;61m".to_string());

        ptui_println!(
            "{}{} Cloning request",
            Ptui::clear_line(),
            Ptui::color_string("Sending:".to_string(), custom)
        );
        Ok((Client { stream }, addr))
    }

    pub(crate) fn clone() -> Result<(), String> {
        type Fgm = ForegroundModifier;
        let custom = Fgm::Custom("\x1b[38;5;61m".to_string());
        ptui_println!(
            "{}",
            Ptui::color_string("FreeSync:".to_string(), custom.clone())
        );
        let mut conn: Client;
        let upstream: String;
        (conn, upstream) = Client::new()?;
        let command = "CLONE\n";

        conn.stream
            .write_all(command.as_bytes())
            .expect("Unable to write command to stream");

        let mut packets: String = String::new();
        let mut reader = BufReader::new(&conn.stream);
        reader.read_line(&mut packets).unwrap();
        let _ = packets.pop();
        drop(reader);
        let packets = packets
            .parse::<i32>()
            .expect("Could not parse packets into a number");

        ptui_println!(
            "{}{} {upstream}",
            Ptui::clear_line(),
            Ptui::color_string("Upstream:".to_string(), custom.clone())
        );
        ptui_println!(
            "{} {packets} objects\n\n",
            Ptui::color_string("Pulling:".to_string(), custom.clone())
        );

        let pool = ThreadPool::new(4);
        let panic = Arc::new(AtomicBool::new(false));
        let stream = Arc::new(Mutex::from(conn.stream));
        let objects = Arc::new(AtomicUsize::new(0));
        for _ in 0..packets {
            let panic = Arc::clone(&panic);
            let stream_c = Arc::clone(&stream);
            let objects_c = Arc::clone(&objects);
            let custom = custom.clone();
            pool.execute(move || {
                let packet = match deserialize_from_stream(
                    &mut stream_c.lock().expect("Failed to get stream"),
                ) {
                    Ok(data) => data,
                    Err(e) => {
                        panic.store(true, Ordering::Relaxed);
                        eprintln!("{}", e);
                        return;
                    }
                };

                if MerkleTree::write_packet(".".into(), packet).is_err() {
                    panic.store(true, Ordering::Relaxed);
                    eprintln!("Failed to write packet");
                }
                objects_c.fetch_add(1, Ordering::SeqCst);
                let str = format!(
                    "{}{} {} objects of {packets}",
                    Ptui::clear_line().repeat(2),
                    Ptui::color_string("Progress:".to_string(), custom),
                    objects_c.load(Ordering::SeqCst)
                );

                ptui_println!(
                    "{str}\n{}",
                    Ptui::progress_bar(
                        ('=', '<', '>'),
                        32,
                        objects_c.load(Ordering::SeqCst),
                        packets as usize
                    )
                );
            })
        }
        pool.join_all();
        if panic.load(Ordering::Relaxed) {
            return Err(String::from("Thread pool panicked"));
        }
        ptui_println!(
            "{}{}",
            Ptui::clear_line(),
            Ptui::color_string("Cloned successfully".to_string(), Fgm::Green)
        );

        Ok(())
    }
    // pub(crate) fn pull() -> Result<(), String> {
    //     let dir = match env::current_dir() {
    //         Ok(dir) => dir,
    //         Err(e) => return Err(e.to_string()),
    //     };
    //
    //     let node = MerkleTree::create(dir).expect("Unable to create tree");
    //     let _hash = TreeNode::hash_to_hex_string(&node.get_hash());
    //     let _addr = match MerkleTree::get_upstream(".".into()) {
    //         Ok(addr) => addr,
    //         Err(e) => panic!("{e}"),
    //     };
    //
    //     let mut conn = Client::new()?;
    //
    //     let command = "GET UPSTREAM\n";
    //
    //     conn.stream.write_all(command.as_bytes()).unwrap();
    //
    //     let mut upstream_hash: String = String::new();
    //     conn.stream.read_to_string(&mut upstream_hash).unwrap();
    //     println!("{upstream_hash}");
    //
    //     Ok(())
    // }
}
