use std::{io::Read, net::TcpStream};

pub enum Packet {
    ObjectFile(Vec<u8>, String),
    HeadFile(String),
    BranchFile(Vec<u8>, String),
}

pub fn serialize(packet: Packet) -> Vec<Vec<u8>> {
    let str: String;
    let data: Vec<u8>;
    let result: Vec<Vec<u8>>;
    match packet {
        Packet::ObjectFile(obj_data, hash) => {
            data = obj_data;
            str = hash;
            result = vec![
                0.to_string().as_bytes().to_vec(),
                format!("{:08}", data.len()).to_string().as_bytes().to_vec(),
                data,
                format!("{:08}", str.len()).to_string().as_bytes().to_vec(),
                str.as_bytes().to_vec(),
            ];
        }
        Packet::HeadFile(name) => {
            result = vec![
                1.to_string().as_bytes().to_vec(),
                format!("{:08}", name.len()).to_string().as_bytes().to_vec(),
                name.as_bytes().to_vec(),
            ];
        }
        Packet::BranchFile(hash, name) => {
            data = hash;
            str = name;

            result = vec![
                2.to_string().as_bytes().to_vec(),
                format!("{:08}", data.len()).to_string().as_bytes().to_vec(),
                data,
                format!("{:08}", str.len()).to_string().as_bytes().to_vec(),
                str.as_bytes().to_vec(),
            ];
        }
    }
    result
}
pub fn deserialize_from_stream(stream: &mut TcpStream) -> Result<Packet, String> {
    let mut buf: Vec<u8> = Vec::new();
    buf.resize(1, 5);
    if let Err(e) = stream.read_exact(&mut buf) {
        return Err(format!("Unable to read stream properly, - Packet type {e}").to_string());
    };
    let packet_type: u8 = match String::from_utf8(buf[..1].to_vec()).unwrap().parse::<u8>() {
        Ok(int) => int,
        Err(e) => return Err(format!("Invalid packet type, {e}").to_string()),
    };

    match packet_type {
        0 => {
            buf.resize(8, 0);
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
            buf.clear();
            buf.resize(length, 0);

            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Object data, {e}").to_string()
                );
            };
            let data = buf.clone();
            buf.resize(8, 0);
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
            buf.clear();
            buf.resize(length, 0);
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

            Ok(Packet::ObjectFile(data, hash))
        }
        1 => {
            buf.resize(8, 0);
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
            buf.clear();
            buf.resize(length, 0);
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
        2 => {
            buf.resize(8, 0);
            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Branch hash length, {e}").to_string(),
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
            buf.clear();
            buf.resize(length, 0);

            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly - Branch hash data, {e}").to_string(),
                );
            };
            let hash = buf.clone();

            buf.resize(8, 0);
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
            buf.clear();
            buf.resize(length, 0);
            if let Err(e) = stream.read_exact(&mut buf) {
                return Err(
                    format!("Unable to read stream properly, - Object hash {e}").to_string()
                );
            };
            let str = match String::from_utf8(buf.as_slice().to_vec()) {
                Ok(val) => val,
                Err(e) => {
                    return Err(format!(
                        "Unable to convert data inside vector to string, - Branch file {e}, {:?}",
                        buf
                    ));
                }
            };
            let name = str;
            Ok(Packet::BranchFile(hash, name))
        }
        _ => Err("Invalid packet type received".to_string()),
    }
}
