// cxx-async/src/main.rs

use crate::ffi::{RustOneshotChannelF64, RustOneshotChannelString};
use async_recursion::async_recursion;
use cxx::CxxString;
use futures::channel::oneshot::{self, Canceled, Receiver, Sender};
use futures::executor::{self, ThreadPool};
use futures::join;
use futures::task::{Spawn, SpawnExt};
use once_cell::sync::Lazy;
use std::cell::UnsafeCell;
use std::error::Error;
use std::fmt::{Debug, Display, Formatter, Result as FmtResult};
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
    pub fn new(what: Box<str>) -> Self {
        Self { what }
    }
    pub fn what(&self) -> &str {
        &self.what
    }
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

trait CxxReceiver {
    type Output;
    type PlainReceiver;
    type PlainSender;
    type CxxSender;
    fn from_plain_receiver(plain_receiver: Self::PlainReceiver) -> Box<Self>;
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

    // Boilerplate for strings
    pub struct RustOneshotChannelString {
        pub sender: Box<RustOneshotSenderString>,
        pub receiver: Box<RustOneshotReceiverString>,
    }
    extern "Rust" {
        type RustOneshotSenderString;
        type RustOneshotReceiverString;
        unsafe fn send(self: &mut RustOneshotSenderString, value: *const String, error: &str);
        unsafe fn recv(
            self: &mut RustOneshotReceiverString,
            maybe_result: *mut String,
            error: Pin<&mut CxxString>,
            coroutine_address: *mut u8,
        ) -> i32;
        fn channel(self: &RustOneshotReceiverString) -> RustOneshotChannelString;
    }

    extern "Rust" {
        fn rust_dot_product() -> Box<RustOneshotReceiverF64>;
        fn rust_not_product() -> Box<RustOneshotReceiverF64>;
        fn rust_cppcoro_ping_pong(i: i32) -> Box<RustOneshotReceiverString>;
        fn rust_folly_ping_pong(i: i32) -> Box<RustOneshotReceiverString>;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("cppcoro_example.h");
        include!("libunifex_example.h");
        include!("folly_example.h");

        unsafe fn rust_resume_cxx_coroutine(address: *mut u8);
        unsafe fn rust_destroy_cxx_coroutine(address: *mut u8);

        fn cppcoro_dot_product() -> Box<RustOneshotReceiverF64>;
        fn cppcoro_call_rust_dot_product();
        fn cppcoro_not_product() -> Box<RustOneshotReceiverF64>;
        fn cppcoro_call_rust_not_product();
        fn cppcoro_ping_pong(i: i32) -> Box<RustOneshotReceiverString>;

        fn libunifex_dot_product() -> Box<RustOneshotReceiverF64>;
        fn libunifex_call_rust_dot_product_with_coro();
        fn libunifex_call_rust_dot_product_directly();
        fn libunifex_not_product() -> Box<RustOneshotReceiverF64>;
        fn libunifex_call_rust_not_product();

        fn folly_dot_product() -> Box<RustOneshotReceiverF64>;
        fn folly_call_rust_dot_product();
        fn folly_not_product() -> Box<RustOneshotReceiverF64>;
        fn folly_call_rust_not_product();
        fn folly_ping_pong(i: i32) -> Box<RustOneshotReceiverString>;
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
                                ptr::copy_nonoverlapping(&result, maybe_result, 1);
                                mem::forget(result);
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
                            ptr::copy_nonoverlapping(&result, maybe_result, 1);
                            mem::forget(result);
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

            impl CxxReceiver for [<RustOneshotReceiver $name>] {
                type Output = $ty;
                type PlainReceiver = Receiver<[<RustOneshotType $name>]>;
                type PlainSender = Sender<[<RustOneshotType $name>]>;
                type CxxSender = [<RustOneshotSender $name>];
                fn from_plain_receiver(plain_receiver: Self::PlainReceiver) -> Box<Self> {
                    Box::new([<RustOneshotReceiver $name>](plain_receiver))
                }
            }
        }
    };
}

trait CxxAsync {
    type Output;
    fn via<Recv, Exec>(self, executor: &Exec) -> Box<Recv>
    where
        Recv: CxxReceiver<
            Output = Self::Output,
            PlainReceiver = Receiver<Result<Self::Output, CxxAsyncException>>,
        >,
        Exec: Spawn;
}

impl<Out, Fut> CxxAsync for Fut
where
    Fut: Future<Output = Result<Out, CxxAsyncException>> + Send + 'static,
    Out: Send + Debug + 'static,
{
    type Output = Out;
    fn via<Recv, Exec>(self, executor: &Exec) -> Box<Recv>
    where
        Recv: CxxReceiver<
            Output = Self::Output,
            PlainReceiver = Receiver<Result<Self::Output, CxxAsyncException>>,
        >,
        Exec: Spawn,
    {
        let (sender, receiver) = oneshot::channel();
        executor.spawn(go(sender, self)).unwrap();
        return CxxReceiver::from_plain_receiver(receiver);

        async fn go<Out, Fut>(sender: Sender<Result<Out, CxxAsyncException>>, fut: Fut)
        where
            Fut: Future<Output = Result<Out, CxxAsyncException>>,
            Out: Debug,
        {
            // FIXME(pcwalton): Is it a bad idea to unwrap here?
            sender.send(fut.await).unwrap();
        }
    }
}

// Application code follows:

static THREAD_POOL: Lazy<ThreadPool> = Lazy::new(|| ThreadPool::new().unwrap());

define_oneshot!(F64, f64);
define_oneshot!(String, String);

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
    async fn go() -> Result<f64, CxxAsyncException> {
        let (ref vector_a, ref vector_b) = *VECTORS;
        Ok(dot_product_inner(&vector_a, &vector_b).await)
    }

    go().via(&*THREAD_POOL)
}

fn rust_not_product() -> Box<RustOneshotReceiverF64> {
    async fn go() -> Result<f64, CxxAsyncException> {
        Err(CxxAsyncException::new("kapow".to_owned().into_boxed_str()))
    }

    go().via(&*THREAD_POOL)
}

fn rust_cppcoro_ping_pong(i: i32) -> Box<RustOneshotReceiverString> {
    async fn go(i: i32) -> Result<String, CxxAsyncException> {
        Ok(format!(
            "{}ping ",
            if i < 8 {
                ffi::cppcoro_ping_pong(i + 1).await.unwrap().unwrap()
            } else {
                String::new()
            }
        ))
    }

    go(i).via(&*THREAD_POOL)
}

fn rust_folly_ping_pong(i: i32) -> Box<RustOneshotReceiverString> {
    async fn go(i: i32) -> Result<String, CxxAsyncException> {
        Ok(format!(
            "{}ping ",
            if i < 8 {
                ffi::folly_ping_pong(i + 1).await.unwrap().unwrap()
            } else {
                String::new()
            }
        ))
    }

    go(i).via(&*THREAD_POOL)
}

fn test_cppcoro() {
    // Test Rust calling C++ async functions.
    let receiver = ffi::cppcoro_dot_product();
    println!("{}", executor::block_on(receiver).unwrap().unwrap());

    // Test C++ calling Rust async functions.
    ffi::cppcoro_call_rust_dot_product();

    // Test exceptions being thrown by C++ async functions.
    let receiver = ffi::cppcoro_not_product();
    match executor::block_on(receiver).unwrap() {
        Ok(_) => panic!("shouldn't have succeeded!"),
        Err(err) => println!("{}", err.what()),
    }

    // Test errors being thrown by Rust async functions.
    ffi::cppcoro_call_rust_not_product();

    // Ping-pong test.
    let receiver = ffi::cppcoro_ping_pong(0);
    println!("{}", executor::block_on(receiver).unwrap().unwrap());
}

fn test_libunifex() {
    // Test Rust calling C++ async functions.
    let receiver = ffi::libunifex_dot_product();
    println!("{}", executor::block_on(receiver).unwrap().unwrap());

    // Test C++ calling Rust async functions.
    ffi::libunifex_call_rust_dot_product_with_coro();
    ffi::libunifex_call_rust_dot_product_directly();

    // Test exceptions being thrown by C++ async functions.
    let receiver = ffi::libunifex_not_product();
    match executor::block_on(receiver).unwrap() {
        Ok(_) => panic!("shouldn't have succeeded!"),
        Err(err) => println!("{}", err.what()),
    }

    // Test errors being thrown by Rust async functions.
    ffi::libunifex_call_rust_not_product();
}

fn test_folly() {
    // Test Rust calling C++ async functions.
    let receiver = ffi::folly_dot_product();
    println!("{}", executor::block_on(receiver).unwrap().unwrap());

    // Test C++ calling Rust async functions.
    ffi::folly_call_rust_dot_product();

    // Test exceptions being thrown by C++ async functions.
    let receiver = ffi::folly_not_product();
    match executor::block_on(receiver).unwrap() {
        Ok(_) => panic!("shouldn't have succeeded!"),
        Err(err) => println!("{}", err.what()),
    }

    // Test errors being thrown by Rust async functions.
    ffi::folly_call_rust_not_product();

    // Ping-pong test.
    let receiver = ffi::folly_ping_pong(0);
    println!("{}", executor::block_on(receiver).unwrap().unwrap());
}

fn main() {
    test_cppcoro();
    test_libunifex();
    test_folly();
}
