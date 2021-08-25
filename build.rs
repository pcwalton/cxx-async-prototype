// cxx-async/build.rs

fn main() {
    cxx_build::bridge("src/main.rs")
        .file("src/example.cpp")
        .flag_if_supported("-std=c++17")
        .include("include")
        .include("cxx-async/src")
        .compile("cxx-async");
}
