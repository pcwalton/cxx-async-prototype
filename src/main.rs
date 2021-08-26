// cxx-async/src/main.rs

use futures::channel::oneshot::{self, Receiver, Sender};
use futures::executor;
use std::ffi::c_void;
use std::future::Future;
use std::pin::Pin;
use std::ptr;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        type RustOneshotI32;
        fn set_value(self: &mut RustOneshotI32, value: i32);
        fn clone_box(self: &RustOneshotI32) -> Box<RustOneshotI32>;
        fn make_oneshot_i32() -> Box<RustOneshotI32>;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("example.h");

        fn my_async_operation() -> Box<RustOneshotI32>;
    }
}

#[derive(Clone)]
pub struct RustOneshotI32(Arc<Mutex<RustOneshotImplI32>>);

struct RustOneshotImplI32 {
    receiver: Receiver<i32>,
    sender: Option<Sender<i32>>,
}

impl RustOneshotI32 {
    pub fn set_value(&mut self, value: i32) {
        drop(
            self.0
                .lock()
                .unwrap()
                .sender
                .take()
                .expect("Can't send twice on a oneshot channel!")
                .send(value),
        );
    }

    pub fn clone_box(&self) -> Box<Self> {
        Box::new((*self).clone())
    }
}

fn make_oneshot_i32() -> Box<RustOneshotI32> {
    let (sender, receiver) = oneshot::channel();
    Box::new(RustOneshotI32(Arc::new(Mutex::new(RustOneshotImplI32 {
        receiver,
        sender: Some(sender),
    }))))
}

impl Future for RustOneshotI32 {
    type Output = i32;
    fn poll(mut self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output> {
        let mut this = self.0.lock().unwrap();
        match Pin::new(&mut this.receiver).poll(context) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(Err(_)) => panic!("TODO: error handling"),
            Poll::Ready(Ok(value)) => Poll::Ready(value),
        }
    }
}

fn main() {
    let receiver = ffi::my_async_operation();
    println!("{}", executor::block_on(receiver));
}
