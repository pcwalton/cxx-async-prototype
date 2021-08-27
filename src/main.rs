// cxx-async/src/main.rs

use futures::channel::oneshot::{self, Receiver, Sender};
use futures::executor;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

macro_rules! define_oneshot {
    ($name:ident, $ty:ty) => {
        paste::paste! {
            #[derive(Clone)]
            pub struct [<RustOneshot $name>](Arc<Mutex<RustOneshotImpl<$ty>>>);

            impl [<RustOneshot $name>] {
                pub fn set_value(&mut self, value: $ty) {
                    self.0.lock().unwrap().set_value(value)
                }

                pub fn clone_box(&self) -> Box<Self> {
                    Box::new((*self).clone())
                }

                // FIXME(pcwalton): The `self` here is fake and is just here so that `cxx` will
                // namespace `make` to this class.
                pub fn make(&self) -> Box<Self> {
                    Box::new([<RustOneshot $name>](RustOneshotImpl::new()))
                }
            }

            impl Future for [<RustOneshot $name>] {
                type Output = $ty;
                fn poll(self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output> {
                    let mut this = self.0.lock().unwrap(); 
                    Pin::new(&mut *this).poll(context)
                }
            }

        }
    }
}

#[cxx::bridge]
mod ffi {
    // Boilerplate for I32
    extern "Rust" {
        type RustOneshotI32;
        fn set_value(self: &mut RustOneshotI32, value: i32);
        fn clone_box(self: &RustOneshotI32) -> Box<RustOneshotI32>;
        fn make(self: &RustOneshotI32) -> Box<RustOneshotI32>;
    }

    // Boilerplate for F64
    extern "Rust" {
        type RustOneshotF64;
        fn set_value(self: &mut RustOneshotF64, value: f64);
        fn clone_box(self: &RustOneshotF64) -> Box<RustOneshotF64>;
        fn make(self: &RustOneshotF64) -> Box<RustOneshotF64>;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("cppcoro_example.h");

        fn dot_product() -> Box<RustOneshotF64>;
        fn my_async_operation() -> Box<RustOneshotI32>;
    }
}

define_oneshot!(I32, i32);
define_oneshot!(F64, f64);

struct RustOneshotImpl<T> {
    receiver: Receiver<T>,
    sender: Option<Sender<T>>,
}

impl<T> RustOneshotImpl<T> {
    fn new() -> Arc<Mutex<RustOneshotImpl<T>>> {
        let (sender, receiver) = oneshot::channel();
        Arc::new(Mutex::new(RustOneshotImpl { receiver, sender: Some(sender) }))
    }

    fn set_value(&mut self, value: T) {
        drop(
            self.sender
                .take()
                .expect("Can't send twice on a oneshot channel!")
                .send(value),
        );
    }

    fn poll(mut self: Pin<&mut Self>, context: &mut Context) -> Poll<T> {
        match Pin::new(&mut self.receiver).poll(context) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(Err(_)) => panic!("TODO: error handling"),
            Poll::Ready(Ok(value)) => Poll::Ready(value),
        }
    }
}

fn main() {
    let receiver = ffi::dot_product();
    println!("{}", executor::block_on(receiver));
}
