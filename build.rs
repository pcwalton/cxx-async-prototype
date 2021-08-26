// cxx-async/build.rs

fn main() {
    cxx_build::bridge("src/main.rs")
        .file("src/example.cpp")
        .flag_if_supported("-std=c++20")
        .include("include")
        .compile("cxx-async");
}
