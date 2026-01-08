use crate::merkle::node::Change;
use flate2::read::ZlibDecoder;
use std::path::PathBuf;


pub(in crate::merkle) trait CompressedData {
    fn compress(data: &Vec<u8>) -> Vec<u8> {
        use flate2::Compression;
        use flate2::write::ZlibEncoder;
        use std::io::prelude::*;

        let mut encoder = ZlibEncoder::new(Vec::new(), Compression::best());
        encoder.write_all(&data).expect("Unable to write data");
        encoder.finish().expect("Unable to finish compression")
    }
    fn decompress(data: &Vec<u8>) -> Vec<u8> {
        use std::io::prelude::*;

        let mut decoder = ZlibDecoder::new(&data[..]);
        let mut decompressed: Vec<u8> = Vec::new();
        decoder
            .read_to_end(&mut decompressed)
            .expect("Error decompressing data");

        decompressed
    }
}
pub(in crate::merkle) trait LeafData: CompressedData {
    fn data(&self) -> &Vec<u8>;
    fn diff_file(&self, other: &Self) -> Vec<Change>;
}

pub trait TreeIO {
    fn init() -> bool;

    fn write_tree(&self) -> bool;
    fn read_tree(path: &PathBuf) -> Result<Self,String> where Self: Sized;
    const MAIN_FOLDER: &'static str = "./.freesync";

    const OBJ_FOLDER: &'static str = "./.freesync/objects";
}

pub(in crate::merkle) trait LeafIO: LeafData {
    fn write_blob(&self);
    fn read_blob(path: &PathBuf) -> Result<Self,String> where Self: Sized;
}
