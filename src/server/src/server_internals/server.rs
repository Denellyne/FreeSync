use std::io::{BufRead, BufReader};
use std::net::{TcpListener, TcpStream};

pub struct Server {
    listener: TcpListener,
}

impl Server {
    pub fn new(port: String) -> Self {
        println!("Starting the Server...");

        if port.parse::<u16>().is_err() {
            panic!("Invalid port number!");
        }

        let ip = format!("0.0.0.0:{}", port);
        let listener = TcpListener::bind(ip).expect("Could not bind port!");

        println!("Info:\nFreeSync Server\nPort:{}", port);
        println!("Server started");
        Server { listener }
    }

    fn close_server(self) {
        println!("\nClosing the Server...");
        println!("Server closed");
    }

    pub fn run_server(self) {
        println!("\nServer running\n");
        for stream in self.listener.incoming() {
            match stream {
                Ok(stream) => handle_connection(stream),
                Err(e) => eprintln!("Unable to establish connection, {}", e),
            }
        }

        self.close_server();
    }

    #[cfg(test)]
    pub fn mock_server(self) -> String {
        println!("\nServer running\n");
        for stream in self.listener.incoming() {
            match stream {
                Ok(stream) => {
                    handle_connection(stream);
                    break;
                }
                Err(e) => eprintln!("Unable to establish connection, {}", e),
            }
        }

        self.close_server();
    }
}

fn handle_connection(stream: TcpStream) {
    let buf_reader = BufReader::new(&stream);
    let http_request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap())
        .take_while(|line| !line.is_empty())
        .collect();

    println!("Request: {http_request:#?}");
}
