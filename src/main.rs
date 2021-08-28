// cxx-async/src/main.rs

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
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

const SPLIT_LIMIT: usize = 32;

macro_rules! define_oneshot {
    ($name:ident, $ty:ty) => {
        paste::paste! {
            #[derive(Clone)]
            pub struct [<RustOneshot $name>](Arc<Mutex<RustOneshotImpl<$ty>>>);

            impl [<RustOneshot $name>] {
                pub fn send(&mut self, value: $ty) {
                    self.0.lock().unwrap().send(value)
                }

                pub fn clone_box(&self) -> Box<Self> {
                    Box::new((*self).clone())
                }

                // FIXME(pcwalton): The `self` here is fake and is just here so that `cxx` will
                // namespace `make` to this class.
                pub fn make(&self) -> Box<Self> {
                    Self::new()
                }

                unsafe fn try_recv(&mut self, maybe_result: *mut $ty) -> bool {
                    self.0.lock().unwrap().try_recv(maybe_result)
                }

                unsafe fn poll_with_coroutine_handle(&mut self,
                                                     maybe_result: *mut f64,
                                                     coroutine_address: *mut u8)
                                                     -> bool {
                    self.0
                        .lock()
                        .unwrap()
                        .poll_with_coroutine_handle(maybe_result, coroutine_address)
                }

                pub fn new() -> Box<Self> {
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
    };
}

#[cxx::bridge]
mod ffi {
    /*
    // Boilerplate for I32
    extern "Rust" {
        type RustOneshotI32;
        fn send(self: &mut RustOneshotI32, value: i32);
        fn clone_box(self: &RustOneshotI32) -> Box<RustOneshotI32>;
        fn make(self: &RustOneshotI32) -> Box<RustOneshotI32>;
    }
    */

    // Boilerplate for F64
    extern "Rust" {
        type RustOneshotF64;
        fn send(self: &mut RustOneshotF64, value: f64);
        fn clone_box(self: &RustOneshotF64) -> Box<RustOneshotF64>;
        fn make(self: &RustOneshotF64) -> Box<RustOneshotF64>;
        unsafe fn try_recv(self: &mut RustOneshotF64, maybe_result: *mut f64) -> bool;
        unsafe fn poll_with_coroutine_handle(self: &mut RustOneshotF64,
                                             maybe_result: *mut f64,
                                             coroutine_address: *mut u8)
                                             -> bool;
    }

    extern "Rust" {
        fn rust_dot_product() -> Box<RustOneshotF64>;
    }

    unsafe extern "C++" {
        include!("cxx_async.h");
        include!("cppcoro_example.h");

        unsafe fn rust_resume_cxx_coroutine(address: *mut u8);
        unsafe fn rust_destroy_cxx_coroutine(address: *mut u8);

        fn dot_product() -> Box<RustOneshotF64>;
        fn call_rust_dot_product();
    }
}

//define_oneshot!(I32, i32);
define_oneshot!(F64, f64);

struct RustOneshotImpl<T> {
    receiver: Receiver<T>,
    sender: Option<Sender<T>>,
}

impl<T> RustOneshotImpl<T> {
    fn new() -> Arc<Mutex<RustOneshotImpl<T>>> {
        let (sender, receiver) = oneshot::channel();
        Arc::new(Mutex::new(RustOneshotImpl {
            receiver,
            sender: Some(sender),
        }))
    }

    fn send(&mut self, value: T) {
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

    unsafe fn try_recv(&mut self, maybe_result: *mut T) -> bool {
        match self.receiver.try_recv() {
            Ok(Some(result)) => {
                *maybe_result = result;
                true
            }
            Ok(None) | Err(_) => false,
        }
    }

    unsafe fn poll_with_coroutine_handle(&mut self,
                                         maybe_result: *mut T,
                                         coroutine_address: *mut u8)
                                         -> bool {
        let waker = CxxCoroutineAddress(UnsafeCell::new(coroutine_address)).into_waker();
        match Pin::new(&mut self.receiver).poll(&mut Context::from_waker(&waker)) {
            Poll::Ready(Ok(result)) => {
                *maybe_result = result;
                true
            }
            Poll::Ready(Err(_)) => todo!(),
            Poll::Pending => false,
        }
    }
}

struct CxxCoroutineAddress(UnsafeCell<*mut u8>);

impl CxxCoroutineAddress {
    unsafe fn into_waker(self) -> Waker {
        return Waker::from_raw(make_raw_waker(Arc::new(self)));

        static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, wake, wake_by_ref, drop_waker);

        fn make_raw_waker(boxed_coroutine_address: Arc<CxxCoroutineAddress>) -> RawWaker {
            RawWaker::new(
                Arc::into_raw(boxed_coroutine_address) as *const u8 as *const (),
                &VTABLE)
        }

        unsafe fn clone(boxed_coroutine_address: *const ()) -> RawWaker {
            let boxed_coroutine_address = Arc::from_raw(
                boxed_coroutine_address as *const CxxCoroutineAddress);
            mem::forget(boxed_coroutine_address.clone());
            make_raw_waker(boxed_coroutine_address)
        }

        unsafe fn wake(boxed_coroutine_address: *const ()) {
            let boxed_coroutine_address = Arc::from_raw(
                boxed_coroutine_address as *const CxxCoroutineAddress);
            let address = boxed_coroutine_address.0.get();
            ffi::rust_resume_cxx_coroutine(mem::replace(&mut *address, ptr::null_mut()));
            let _ = boxed_coroutine_address;
        }

        unsafe fn wake_by_ref(boxed_coroutine_address: *const ()) {
            let boxed_coroutine_address = Arc::from_raw(
                boxed_coroutine_address as *const CxxCoroutineAddress);
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

static VECTOR_A: [f64; 16384] = [1.0; 16384];
static VECTOR_B: [f64; 16384] = [2.0; 16384];

#[async_recursion]
async fn dot_product_inner(a: &[f64], b: &[f64]) -> f64 {
    if a.len() > SPLIT_LIMIT {
        let half_count = a.len() / 2;
        let (first, second) =
            join!(dot_product_inner(&a[0..half_count], &b[0..half_count]),
                  dot_product_inner(&a[half_count..],  &b[half_count..]));
        return first + second;
    }

    let mut sum = 0.0;
    for (&a, &b) in a.iter().zip(b.iter()) {
        sum += a * b;
    }
    sum
}

fn rust_dot_product() -> Box<RustOneshotF64> {
    // FIXME(pcwalton): Leaking isn't great here.
    let thread_pool = Box::leak(Box::new(ThreadPool::new().unwrap()));
    let oneshot = RustOneshotF64::new();
    thread_pool.spawn(go(oneshot.clone_box())).unwrap();
    return oneshot;

    async fn go(mut oneshot: Box<RustOneshotF64>) {
        let result = dot_product_inner(&VECTOR_A, &VECTOR_B).await;
        println!("{}", result);
        oneshot.send(result);
    }
}

fn main() {
    // Test Rust calling C++ async functions.
    //let receiver = ffi::dot_product();
    //println!("{}", executor::block_on(receiver));

    // Test C++ calling Rust async functions.
    ffi::call_rust_dot_product();
}
