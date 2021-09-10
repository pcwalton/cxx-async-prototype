// cxx-async/build.rs

fn main() {
    println!("cargo:rustc-link-search=../../cppcoro/build/darwin_x64_clang12.0.5_debug/lib");
    println!("cargo:rustc-link-lib=cppcoro");
    cxx_build::bridge("src/main.rs")
        .file("src/cxx_async.cpp")
        .file("src/cppcoro_example.cpp")
        .file("src/example_common.cpp")
        .flag_if_supported("-std=c++20")
        .include("include")
        .include("../../cppcoro/include")
        .compile("cxx-async");
}
