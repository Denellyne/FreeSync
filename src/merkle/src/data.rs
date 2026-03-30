use std::{io::Read, net::TcpStream};

pub enum Packet {
    ObjectFile(Vec<u8>, String),
    HeadFile(String),
    BranchFile(Vec<u8>, String),
}

pub fn serialize(packet: Packet) -> Vec<u8> {
    let mut result = Vec::new();

    match packet {
        Packet::ObjectFile(data, hash) => {
            result.extend_from_slice(b"0");
            result.extend_from_slice(format!("{:032}", data.len()).as_bytes());
            result.extend_from_slice(&data);
            result.extend_from_slice(format!("{:032}", hash.len()).as_bytes());
            result.extend_from_slice(hash.as_bytes());
        }
        Packet::HeadFile(name) => {
            result.extend_from_slice(b"1");
            result.extend_from_slice(format!("{:032}", name.len()).as_bytes());
            result.extend_from_slice(name.as_bytes());
        }
        Packet::BranchFile(hash, name) => {
            result.extend_from_slice(b"2");
            result.extend_from_slice(format!("{:032}", hash.len()).as_bytes());
            result.extend_from_slice(&hash);
            result.extend_from_slice(format!("{:032}", name.len()).as_bytes());
            result.extend_from_slice(name.as_bytes());
        }
    }

    result
}
pub fn deserialize_from_stream(stream: &mut TcpStream) -> Result<Packet, String> {
    let mut buf = vec![0; 1];

    if let Err(e) = stream.read_exact(&mut buf) {
        return Err(format!("Unable to read stream properly, - Packet type {e}").to_string());
    };
    let packet_type: u8 = match String::from_utf8(buf[..1].to_vec()).unwrap().parse::<u8>() {
        Ok(int) => int,
        Err(e) => return Err(format!("Invalid packet type, {e}").to_string()),
    };

    match packet_type {
        0 | 2 => {
            let mut buf = vec![0; 32];
            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Object data length, {e}").to_string(),
                );
            };
            let str = match String::from_utf8(buf.as_slice().to_vec()) {
                Ok(val) => val,
                Err(e) => {
                    return Err(format!(
                        "Unable to convert data inside vector to string, Object Data {e}, {:?}",
                        buf
                    ));
                }
            };
            let length: usize = match str.parse::<usize>() {
                Ok(int) => int,
                Err(e) => return Err(format!("Invalid length, {e}").to_string()),
            };
            let mut buf = vec![0; length];

            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Object data, {e}").to_string()
                );
            };
            let data = buf;
            let mut buf = vec![0; 32];

            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Object data length, {e}").to_string(),
                );
            };
            let str = match String::from_utf8(buf.as_slice().to_vec()) {
                Ok(val) => val,
                Err(e) => {
                    return Err(format!(
                        "Unable to convert data inside vector to string, lenth {e}, {:?}",
                        buf
                    ));
                }
            };
            let length: usize = match str.parse::<usize>() {
                Ok(int) => int,
                Err(e) => return Err(format!("Invalid length, {e}").to_string()),
            };
            let mut buf = vec![0; length];
            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly, - Object hash {e}").to_string()
                );
            };

            let str = match String::from_utf8(buf.as_slice().to_vec()) {
                Ok(val) => val,
                Err(e) => {
                    return Err(format!(
                        "Unable to convert data inside vector to string, - Object File {e}, {:?}",
                        buf
                    ));
                }
            };
            let hash = str;
            if packet_type == 0 {
                Ok(Packet::ObjectFile(data, hash))
            } else {
                Ok(Packet::BranchFile(data, hash))
            }
        }
        1 => {
            let mut buf = vec![0; 32];
            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Object data length, {e}").to_string(),
                );
            };
            let str = match String::from_utf8(buf.as_slice().to_vec()) {
                Ok(val) => val,
                Err(e) => {
                    return Err(format!(
                        "Unable to convert data inside vector to string, Branch hash {e}, {:?}",
                        buf
                    ));
                }
            };
            let length: usize = match str.parse::<usize>() {
                Ok(int) => int,
                Err(e) => return Err(format!("Invalid length, {e}").to_string()),
            };
            let mut buf = vec![0; length];
            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly, - Object hash {e}").to_string()
                );
            };
            let str = match String::from_utf8(buf.as_slice().to_vec()) {
                Ok(val) => val,
                Err(e) => {
                    return Err(format!(
                        "Unable to convert data inside vector to string, - Head File {e}, {:?}",
                        buf
                    ));
                }
            };
            let name = str;
            Ok(Packet::HeadFile(name))
        }
        _ => Err("Invalid packet type received".to_string()),
    }
}
