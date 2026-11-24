# Continuing native M1 HV development kernel. This is not a bootable image.
{ mkPureDarwinBuild, kernelSource, arm64CrossToolchain }:
mkPureDarwinBuild {
  pname = "puredarwin-kernel-arm64-t8103-bringup";
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
