// cxx-async/build.rs

fn main() {
    // FIXME(pcwalton): Find cppcoro and libunifex better?
    println!("cargo:rustc-link-search=../../cppcoro/build/darwin_x64_clang12.0.5_debug/lib");
    println!("cargo:rustc-link-search=../../libunifex/build/source");
    println!("cargo:rustc-link-search=/usr/local/Cellar/folly/2021.09.06.00/lib");
    println!("cargo:rustc-link-search=/usr/local/Cellar/glog/0.5.0/lib");
    println!("cargo:rustc-link-lib=cppcoro");
    println!("cargo:rustc-link-lib=unifex");
    println!("cargo:rustc-link-lib=glog");
    println!("cargo:rustc-link-lib=folly");
    cxx_build::bridge("src/main.rs")
        .file("src/cxx_async.cpp")
        .file("src/example_common.cpp")
        .file("src/cppcoro_example.cpp")
        .file("src/libunifex_example.cpp")
        .file("src/folly_example.cpp")
        .flag_if_supported("-std=gnu++2a")
        .flag_if_supported("-fcoroutines-ts")
        .include("include")
        .include("../../cppcoro/include")
        .include("../../libunifex/include")
        .include("../../libunifex/build/include")
        .compile("cxx-async");
}
