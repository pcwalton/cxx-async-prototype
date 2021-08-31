// cxx-async/src/main.rs

use crate::ffi::RustOneshotChannelF64;
use async_recursion::async_recursion;
use futures::channel::oneshot::{self, Receiver, Sender};
use futures::executor::{self, ThreadPool};
use futures::join;
use futures::task::SpawnExt;
use std::cell::UnsafeCell;
use std::future::Future;
use std::mem;
use std::pin::Pin;
use std::ptr;
use std::sync::Arc;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

const SPLIT_LIMIT: usize = 32;

struct CxxCoroutineAddress(UnsafeCell<*mut u8>);

impl CxxCoroutineAddress {
    unsafe fn into_waker(self) -> Waker {
        return Waker::from_raw(make_raw_waker(Arc::new(self)));

        static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, wake, wake_by_ref, drop_waker);

        fn make_raw_waker(boxed_coroutine_address: Arc<CxxCoroutineAddress>) -> RawWaker {
            RawWaker::new(
                Arc::into_raw(boxed_coroutine_address) as *const u8 as *const (),
                &VTABLE,
            )
        }

        unsafe fn clone(boxed_coroutine_address: *const ()) -> RawWaker {
            let boxed_coroutine_address =
                Arc::from_raw(boxed_coroutine_address as *const CxxCoroutineAddress);
            mem::forget(boxed_coroutine_address.clone());
            make_raw_waker(boxed_coroutine_address)
        }

        unsafe fn wake(boxed_coroutine_address: *const ()) {
            let boxed_coroutine_address =
                Arc::from_raw(boxed_coroutine_address as *const CxxCoroutineAddress);
            let address = boxed_coroutine_address.0.get();
            ffi::rust_resume_cxx_coroutine(mem::replace(&mut *address, ptr::null_mut()));
            let _ = boxed_coroutine_address;
        }

        unsafe fn wake_by_ref(boxed_coroutine_address: *const ()) {
            let boxed_coroutine_address =
                Arc::from_raw(boxed_coroutine_address as *const CxxCoroutineAddress);
            let address = boxed_coroutine_address.0.get();
            ffi::rust_resume_cxx_coroutine(mem::replace(&mut *address, ptr::null_mut()));
            mem::forget(boxed_coroutine_address);
        }

        unsafe fn drop_waker(boxed_coroutine_address: *const ()) {
            let _ = Arc::from_raw(boxed_coroutine_address as *const CxxCoroutineAddress);
        }
    }
}

impl Drop for CxxCoroutineAddress {
    fn drop(&mut self) {
        unsafe {
            let address = self.0.get();
            ffi::rust_destroy_cxx_coroutine(mem::replace(&mut *address, ptr::null_mut()))
        }
    }
}


#[cxx::bridge]
mod ffi {
    // Boilerplate for F64
    pub struct RustOneshotChannelF64 {
        pub sender: Box<RustOneshotSenderF64>,
        pub receiver: Box<RustOneshotReceiverF64>,
    }
    extern "Rust" {
        type RustOneshotSenderF64;
        type RustOneshotReceiverF64;
        fn send(self: &mut RustOneshotSenderF64, value: f64);
        unsafe fn recv(
            self: &mut RustOneshotReceiverF64,
            maybe_result: *mut f64,
            coroutine_address: *mut u8,
        ) -> bool;
        fn channel(self: &RustOneshotReceiverF64) -> RustOneshotChannelF64;
    }

    extern "Rust" {
        fn rust_dot_product() -> Box<RustOneshotReceiverF64>;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("cppcoro_example.h");

        unsafe fn rust_resume_cxx_coroutine(address: *mut u8);
        unsafe fn rust_destroy_cxx_coroutine(address: *mut u8);

        fn dot_product() -> Box<RustOneshotReceiverF64>;
        fn call_rust_dot_product();
    }
}

macro_rules! define_oneshot {
    ($name:ident, $ty:ty) => {
        paste::paste! {
            pub struct [<RustOneshotSender $name>](Option<Sender<$ty>>);

            pub struct [<RustOneshotReceiver $name>](Receiver<$ty>);

            impl [<RustOneshotSender $name>] {
                pub fn send(&mut self, value: $ty) {
                    self.0.take().unwrap().send(value).unwrap();
                }
            }

            impl [<RustOneshotReceiver $name>] {
                fn channel(&self) -> [<RustOneshotChannel $name>] {
                    let (sender, receiver) = oneshot::channel();
                    [<RustOneshotChannel $name>] {
                        sender: Box::new([<RustOneshotSender $name>](Some(sender))),
                        receiver: Box::new([<RustOneshotReceiver $name>](receiver)),
                    }
                }

                unsafe fn recv(&mut self, maybe_result: *mut $ty, coroutine_address: *mut u8)
                        -> bool {
                    if coroutine_address.is_null() {
                        match self.0.try_recv() {
                            Ok(Some(result)) => {
                                *maybe_result = result;
                                return true
                            }
                            Ok(None) | Err(_) => return false,
                        }
                    }

                    let waker =
                        CxxCoroutineAddress(UnsafeCell::new(coroutine_address)).into_waker();
                    match Pin::new(&mut self.0).poll(&mut Context::from_waker(&waker)) {
                        Poll::Ready(Ok(result)) => {
                            *maybe_result = result;
                            true
                        }
                        Poll::Ready(Err(_)) => todo!(),
                        Poll::Pending => false,
                    }
                }
            }

            impl Future for [<RustOneshotReceiver $name>] {
                type Output = $ty;
                fn poll(mut self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output> {
                    match Pin::new(&mut self.0).poll(context) {
                        Poll::Pending => Poll::Pending,
                        Poll::Ready(value) => Poll::Ready(value.unwrap()),
                    }
                }
            }
        }
    }
}

// Application code follows:

define_oneshot!(F64, f64);

static VECTOR_A: [f64; 16384] = [1.0; 16384];
static VECTOR_B: [f64; 16384] = [2.0; 16384];

#[async_recursion]
async fn dot_product_inner(a: &[f64], b: &[f64]) -> f64 {
    if a.len() > SPLIT_LIMIT {
        let half_count = a.len() / 2;
        let (first, second) = join!(
            dot_product_inner(&a[0..half_count], &b[0..half_count]),
            dot_product_inner(&a[half_count..], &b[half_count..])
        );
        return first + second;
    }

    let mut sum = 0.0;
    for (&a, &b) in a.iter().zip(b.iter()) {
        sum += a * b;
    }
    sum
}

fn rust_dot_product() -> Box<RustOneshotReceiverF64> {
    // FIXME(pcwalton): Leaking isn't great here.
    let thread_pool = Box::leak(Box::new(ThreadPool::new().unwrap()));
    let (sender, receiver) = oneshot::channel();
    thread_pool.spawn(go(sender)).unwrap();
    return Box::new(RustOneshotReceiverF64(receiver));

    async fn go(sender: Sender<f64>) {
        sender
            .send(dot_product_inner(&VECTOR_A, &VECTOR_B).await)
            .unwrap();
    }
}

fn main() {
    // Test Rust calling C++ async functions.
    let receiver = ffi::dot_product();
    println!("{}", executor::block_on(receiver));

    // Test C++ calling Rust async functions.
    ffi::call_rust_dot_product();
}
