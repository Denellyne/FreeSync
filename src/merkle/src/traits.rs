use flate2::read::ZlibDecoder;
use std::fs;
use std::fs::ReadDir;
use std::path::Path;

pub trait Hashable {
    fn hash(vec: &[u8]) -> [u8; 32];
    fn hash_to_hex_string(hash: &[u8; 32]) -> String {
        hash.iter().map(|b| format!("{:02x}", b)).collect()
    }

    fn get_hash(&self) -> [u8; 32];
}

pub trait CompressedData {
    fn compress(data: &[u8]) -> Result<Vec<u8>, String> {
        use flate2::Compression;
        use flate2::write::ZlibEncoder;
        use std::io::prelude::*;

        let mut encoder = ZlibEncoder::new(Vec::new(), Compression::best());
        match encoder.write_all(data) {
            Ok(_) => match encoder.finish() {
                Ok(data) => Ok(data),
                Err(_) => Err(String::from("Failed to flush compressed data")),
            },
            Err(_) => Err(String::from("Failed to compress")),
        }
    }
    fn decompress(data: &[u8]) -> Result<Vec<u8>, String> {
        use std::io::prelude::*;

        let mut decoder = ZlibDecoder::new(data);
        let mut decompressed: Vec<u8> = Vec::new();
        match decoder.read_to_end(&mut decompressed) {
            Ok(_) => Ok(decompressed),
            Err(_) => Err(String::from("Failed to decompress")),
        }
    }
}

pub trait ReadFile {
    fn read_file(path: impl AsRef<Path>) -> Result<Vec<u8>, String> {
        match fs::read(&path) {
            Ok(data) => Ok(data),
            Err(e) => Err(format!("{} Path:{}", e, path.as_ref().display())),
        }
    }

    fn read_dir(path: impl AsRef<Path>) -> Result<ReadDir, String> {
        let paths = fs::read_dir(&path);
        match paths {
            Ok(paths) => Ok(paths),
            Err(e) => Err(e.to_string()),
        }
    }

    fn read_until_null(mut data: Vec<u8>) -> Result<(Vec<u8>, Vec<u8>), String> {
        if let Some(pos) = data.iter().position(|&b| b == 0) {
            let head: Vec<u8> = data.drain(0..pos).collect();
            data.drain(0..1);
            return Ok((head, data));
        }
        Err("Unable to read until null-byte".to_owned())
    }
}
