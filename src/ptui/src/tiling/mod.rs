use std::sync::{Arc, Mutex};
use std::sync::atomic::AtomicUsize;
use crate::modifiers::TextModifier;

pub struct ProgressBar{
  ui : (char,char,char),
  current : Arc<AtomicUsize>,
  total : usize,
}

pub struct Line{
  string : String,
  modifier : Option<TextModifier>,
}

pub enum Tile{
  Line(Line),
  ProgressBar(ProgressBar),
  Pane(Pane)
}
pub struct Pane{
  width : usize,
  height : usize,
  pos : (usize, usize),
  tiles : Vec<Mutex<Tile>>
}

impl Pane{
  pub fn new(width : usize, height : usize,pos : (usize,usize)) -> Self{
    Pane{
      width,
      height,
      pos,
      tiles: vec![]
    }
  }
  pub fn remove_tile(&mut self, idx: usize){
    self.tiles.remove(idx);
  }
  pub fn push_tile(&mut self, tile : Tile) ->  &Mutex<Tile>{
    self.tiles.push(Mutex::new(tile));
     &self.tiles[self.tiles.len()-1]
  }
  pub fn get_tile(&self, idx: usize) -> Option<&Mutex<Tile>>{
    if idx >= self.tiles.len(){
      return None
    }
    Some(&self.tiles[idx])
  }
  
  pub(crate)fn print(&self){
    
  }


}
impl Line{
  pub fn new(string : String, modifier : Option<TextModifier>) -> Self{
    Line{
      string,
      modifier,
    }
  }
  pub fn set_string(&mut self, text : String){
    self.string = text;
  }
}

