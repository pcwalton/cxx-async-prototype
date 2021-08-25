// cxx-async/src/main.rs

use ffi::RustChannelI32;
use futures::channel::oneshot::{self, Receiver, Sender};
use futures::executor;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};

#[cxx::bridge]
mod ffi {
    /*
    struct CxxFutureI32 {
        m_impl: SharedPtr<CxxFutureImplI32>,
    }*/
    struct RustChannelI32 {
        receiver: Box<RustReceiverI32>,
        sender: Box<RustSenderI32>,
    }

    extern "Rust" {
        type RustReceiverI32;

        type RustSenderI32;
        fn set_value(self: &mut RustSenderI32, value: i32);

        fn make_channel() -> RustChannelI32;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("example.h");

        fn my_async_operation() -> Box<RustReceiverI32>;
    }
}

pub struct RustReceiverI32(Receiver<i32>);
pub struct RustSenderI32 {
    sender: Option<Sender<i32>>,
}

impl RustSenderI32 {
    fn set_value(&mut self, value: i32) {
        drop(self.sender.take().expect("Can't send twice on a oneshot channel!").send(value));
    }
}

fn make_channel() -> RustChannelI32 {
    let (sender, receiver) = oneshot::channel();
    RustChannelI32 {
        receiver: Box::new(RustReceiverI32(receiver)),
        sender: Box::new(RustSenderI32 { sender: Some(sender) }),
    }
}

impl Future for RustReceiverI32 {
    type Output = i32;
    fn poll(mut self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output> {
        match Pin::new(&mut self.0).poll(context) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(Err(_)) => panic!("TODO: error handling"),
            Poll::Ready(Ok(value)) => Poll::Ready(value),
        }
    }
}

/*
impl CxxFutureI32 {
    delegate! {
        to self.m_impl {
            pub fn valid(&self) -> bool;
        }
    }
}

impl Future for CxxFutureI32 {
    type Output = i32;
    fn poll(self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output> {
        let (sender, receiver) = oneshot::channel();
        self.m_impl.then(|result| drop(sender.send(result)));
        receiver.poll(context)
    }
}
*/

fn main() {
    let receiver = ffi::my_async_operation();
    executor::block_on(receiver);
}
