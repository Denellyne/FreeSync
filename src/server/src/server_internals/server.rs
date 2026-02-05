use std::io::{BufRead, BufReader};
use std::net::{TcpListener, TcpStream};
// The only uses of expect and unwrap should be at the startup,after that there shall be no unwraps
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

        println!("Info:\nFreeSync Server\nIp:{}", listener.local_addr().expect("Could not get local address"));
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
                Ok(stream) => {
                    let request = handle_connection(stream);
                    println!("Request: {:?}", request);
                }
                Err(e) => eprintln!("Unable to establish connection, {}", e),
            }
        }

        self.close_server();
    }

    #[cfg(test)]
    pub(super) fn mock_server(self) -> Vec<String> {
        println!("\nServer running\n");
        let mut request: Vec<String> = Vec::new();
        if let Some(stream) = self.listener.incoming().next() {
            match stream {
                Ok(stream) => {
                    request = handle_connection(stream);
                    self.close_server();
                    return request;
                }
                Err(e) => panic!("Unable to establish connection, {}", e),
            }
        }
        request
    }
}

fn handle_connection(stream: TcpStream) -> Vec<String> {
    let buf_reader = BufReader::new(&stream);

    let request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap())
        .take_while(|line| !line.is_empty())
        .collect();
    request
}
