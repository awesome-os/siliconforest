// Links the kernel model, built by run-model-tests.sh into $SFMODEL_LIBDIR/libsfspkmodel.a.
fn main() {
    println!("cargo:rerun-if-env-changed=SFMODEL_LIBDIR");
    println!("cargo:rerun-if-env-changed=SFMODEL_SAN");
    let dir = std::env::var("SFMODEL_LIBDIR").expect("SFMODEL_LIBDIR (use run-model-tests.sh)");
    println!("cargo:rerun-if-changed={}/libsfspkmodel.a", dir);
    println!("cargo:rustc-link-search=native={}", dir);
    println!("cargo:rustc-link-lib=static=sfspkmodel");
    if std::env::var("SFMODEL_SAN").as_deref() == Ok("1") {
        /* gcc's sanitizer runtimes, after the archive that needs them */
        println!("cargo:rustc-link-lib=dylib=asan");
        println!("cargo:rustc-link-lib=dylib=ubsan");
    }
}
