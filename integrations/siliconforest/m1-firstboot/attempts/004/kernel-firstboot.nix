# A native T8103 XNU Mach-O with a mandatory pre-VM console-and-park entry.
# This does not produce a KC, filesystem, installer or hardware runner.
{ mkPureDarwinBuild, kernelSource, arm64CrossToolchain }:
mkPureDarwinBuild {
  pname = "puredarwin-kernel-arm64-t8103-firstboot";
  src = kernelSource;
  buildTargets = [ "xnu" ];
  enableUserspace = false;
  installUserland = false;
  installKernel = true;
  xnuKernelConfig = "DEBUG";
  puredarwinArch = "arm64";
  inherit arm64CrossToolchain;
  extraCmakeFlags = [ "-DPUREDARWIN_ARM64_MACHINE_CONFIG=T8103" ];
}
