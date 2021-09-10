// cxx-async/src/main.rs

use crate::ffi::RustOneshotChannelF64;
use async_recursion::async_recursion;
use cxx::CxxString;
use futures::channel::oneshot::{self, Canceled, Receiver, Sender};
use futures::executor::{self, ThreadPool};
use futures::join;
use futures::task::SpawnExt;
use once_cell::sync::Lazy;
use std::cell::UnsafeCell;
use std::error::Error;
use std::fmt::{Display, Formatter, Result as FmtResult};
use std::future::Future;
use std::mem::{self, MaybeUninit};
use std::pin::Pin;
use std::ptr;
use std::sync::Arc;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

const SPLIT_LIMIT: usize = 32;

const RECV_RESULT_PENDING: i32 = 0;
const RECV_RESULT_READY: i32 = 1;
const RECV_RESULT_ERROR: i32 = 2;

#[derive(Debug)]
pub struct CxxAsyncException {
    what: Box<str>,
}

impl CxxAsyncException {
    pub fn new(what: Box<str>) -> Self { Self { what } }
    pub fn what(&self) -> &str { &self.what }
}

impl Display for CxxAsyncException {
    fn fmt(&self, formatter: &mut Formatter) -> FmtResult {
        formatter.write_str(&self.what)
    }
}

impl Error for CxxAsyncException {}

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
        unsafe fn send(self: &mut RustOneshotSenderF64, value: *const f64, error: &str);
        unsafe fn recv(
            self: &mut RustOneshotReceiverF64,
            maybe_result: *mut f64,
            error: Pin<&mut CxxString>,
            coroutine_address: *mut u8,
        ) -> i32;
        fn channel(self: &RustOneshotReceiverF64) -> RustOneshotChannelF64;
    }

    extern "Rust" {
        fn rust_dot_product() -> Box<RustOneshotReceiverF64>;
        fn rust_not_product() -> Box<RustOneshotReceiverF64>;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("cppcoro_example.h");

        unsafe fn rust_resume_cxx_coroutine(address: *mut u8);
        unsafe fn rust_destroy_cxx_coroutine(address: *mut u8);

        fn dot_product() -> Box<RustOneshotReceiverF64>;
        fn call_rust_dot_product();
        fn not_product() -> Box<RustOneshotReceiverF64>;
        fn call_rust_not_product();
    }
}

macro_rules! define_oneshot {
    ($name:ident, $ty:ty) => {
        paste::paste! {
            pub type [<RustOneshotType $name>] = Result<$ty, CxxAsyncException>;

            pub struct [<RustOneshotSender $name>](Option<Sender<[<RustOneshotType $name>]>>);

            pub struct [<RustOneshotReceiver $name>](Receiver<[<RustOneshotType $name>]>);

            impl [<RustOneshotSender $name>] {
                unsafe fn send(&mut self, value: *const $ty, error: &str) {
                    let to_send;
                    if !value.is_null() {
                        let mut staging: MaybeUninit<$ty> = MaybeUninit::uninit();
                        ptr::copy_nonoverlapping(value, staging.as_mut_ptr(), 1);
                        to_send = Ok(staging.assume_init());
                    } else {
                        to_send = Err(CxxAsyncException::new(error.to_owned().into_boxed_str()));
                    }
                    
                    self.0.take().unwrap().send(to_send).unwrap();
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

                unsafe fn recv(&mut self,
                               maybe_result: *mut $ty,
                               error: Pin<&mut CxxString>,
                               coroutine_address: *mut u8)
                               -> i32 {
                    if coroutine_address.is_null() {
                        match self.0.try_recv() {
                            Ok(Some(Ok(result))) => {
                                *maybe_result = result;
                                return RECV_RESULT_READY
                            }
                            Ok(Some(Err(exception))) => {
                                error.push_str(exception.what());
                                return RECV_RESULT_ERROR
                            }
                            Err(Canceled) => {
                                error.push_str("Cancelled (sender dropped)");
                                return RECV_RESULT_ERROR
                            }
                            Ok(None) => return RECV_RESULT_PENDING,
                        }
                    }

                    let waker =
                        CxxCoroutineAddress(UnsafeCell::new(coroutine_address)).into_waker();
                    match Pin::new(&mut self.0).poll(&mut Context::from_waker(&waker)) {
                        Poll::Ready(Ok(Ok(result))) => {
                            *maybe_result = result;
                            return RECV_RESULT_READY
                        }
                        Poll::Ready(Ok(Err(exception))) => {
                            error.push_str(exception.what());
                            return RECV_RESULT_ERROR
                        }
                        Poll::Ready(Err(Canceled)) => {
                            error.push_str("Cancelled (sender dropped)");
                            return RECV_RESULT_ERROR
                        }
                        Poll::Pending => return RECV_RESULT_PENDING,
                    }
                }
            }

            impl Future for [<RustOneshotReceiver $name>] {
                type Output = Result<[<RustOneshotType $name>], Canceled>;
                fn poll(mut self: Pin<&mut Self>, context: &mut Context) -> Poll<Self::Output> {
                    match Pin::new(&mut self.0).poll(context) {
                        Poll::Pending => Poll::Pending,
                        Poll::Ready(value) => Poll::Ready(value),
                    }
                }
            }
        }
    }
}

// Application code follows:

static THREAD_POOL: Lazy<ThreadPool> = Lazy::new(|| ThreadPool::new().unwrap());

define_oneshot!(F64, f64);

struct Xorshift {
    state: u32,
}

impl Xorshift {
    fn new() -> Xorshift {
        Xorshift { state: 0x243f6a88 }
    }

    fn next(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.state = x;
        x
    }
}

static VECTORS: Lazy<(Vec<f64>, Vec<f64>)> = Lazy::new(|| {
    let mut rand = Xorshift::new();
    let (mut vector_a, mut vector_b) = (vec![], vec![]);
    for _ in 0..16384 {
        vector_a.push(rand.next() as f64);
        vector_b.push(rand.next() as f64);
    }
    (vector_a, vector_b)
});

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
    let (sender, receiver) = oneshot::channel();
    THREAD_POOL.spawn(go(sender)).unwrap();
    return Box::new(RustOneshotReceiverF64(receiver));

    async fn go(sender: Sender<RustOneshotTypeF64>) {
        let (ref vector_a, ref vector_b) = *VECTORS;
        sender
            .send(Ok(dot_product_inner(&vector_a, &vector_b).await))
            .unwrap();
    }
}

fn rust_not_product() -> Box<RustOneshotReceiverF64> {
    let (sender, receiver) = oneshot::channel();
    THREAD_POOL.spawn(go(sender)).unwrap();
    return Box::new(RustOneshotReceiverF64(receiver));

    async fn go(sender: Sender<RustOneshotTypeF64>) {
        sender.send(Err(CxxAsyncException::new("kapow".to_owned().into_boxed_str()))).unwrap();
    }
}

fn main() {
    // Test Rust calling C++ async functions.
    let receiver = ffi::dot_product();
    println!("{}", executor::block_on(receiver).unwrap().unwrap());

    // Test C++ calling Rust async functions.
    ffi::call_rust_dot_product();

    // Test exceptions being thrown by C++ async functions.
    let receiver = ffi::not_product();
    match executor::block_on(receiver).unwrap() {
        Ok(_) => panic!("shouldn't have succeeded!"),
        Err(err) => println!("{}", err.what()),
    }

    // Test errors being thrown by Rust async functions.
    ffi::call_rust_not_product();
}
