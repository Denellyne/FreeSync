use merkle::data::deserialize_from_stream;
use merkle::merkletree::MerkleTree;
use std::env;
use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use threadpool::pool::ThreadPool;

pub struct Client {
    stream: TcpStream,
}

impl Client {
    fn new() -> Result<Client, String> {
        let dir = match env::current_dir() {
            Ok(dir) => dir,
            Err(e) => return Err(e.to_string()),
        };

        let addr = match MerkleTree::get_upstream(dir) {
            Ok(addr) => addr,
            Err(e) => return Err(e.to_string()),
        };
        let stream = TcpStream::connect(&addr)
            .unwrap_or_else(|_| panic!("Failed to connect to server,Upstream : {addr}"));
        Ok(Client { stream })
    }

    pub(crate) fn clone() -> Result<(), String> {
        let mut conn = Client::new()?;
        let command = "CLONE\n\n";

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
        println!("Objects {packets}");

        let pool = ThreadPool::new(4);
        let panic = Arc::new(AtomicBool::new(false));
        let stream = Arc::new(Mutex::from(conn.stream));
        for _ in 0..packets {
            let panic = Arc::clone(&panic);
            let stream_c = Arc::clone(&stream);
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
            })
        }
        pool.join_all();
        if panic.load(Ordering::Relaxed) {
            return Err(String::from("Thread pool panicked"));
        }

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
    //     let command = "GET UPSTREAM\n\n";
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
