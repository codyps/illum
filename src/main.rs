#![deny(deprecated)]

use tokio::net::TcpStream;
use tokio::prelude::*;

use std::error::Error;

struct Illum {
    inputs: Vec<Input>,
    backlights: Vec<Backlight>,    
}

struct Conf {
    linearity: u32,
}

struct Input {
    sys_path: OsString,
    // evdev handle
}

struct Backlight {

}

impl Illum {
    fn new() -> Result<Self, Box<dyn Error>> {
        let udev = tokio_udev::Context::new()?;
        let mut e_backlight = tokio_udev::Enumerator::new(&udev)?;
        e_backlight.match_subsystem("backlight")?;
        let backlights = e_backlight.scan_devices()?;
        let mut e_input = tokio_udev::Enumerator::new(&udev)?;
        e_input.match_subsystem("input")?;
        let inputs = e_input.scan_devices()?;
    }
}

impl Backlight {
    fn new_from_dev(
}


#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {





    let mut stream = TcpStream::connect("127.0.0.1:6142").await.unwrap();
    println!("created stream");

    let result = stream.write(b"hello world\n").await;
    println!("wrote to stream; success={:?}", result.is_ok());
}
