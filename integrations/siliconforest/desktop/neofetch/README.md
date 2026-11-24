# neofetch on SiliconForest

`neofetch-7.1.0-siliconforest.patch` is a patch against neofetch 7.1.0 (MIT, Copyright (c) 2015-2020
Dylan Araps; the licence text is at the top of the script and stays there). It adds:

- the SiliconForest mark as the ASCII logo (the conifer of circuit traces from
  `../branding/siliconforest-mark.svg`);
- detection: `sw_vers` reports "SiliconForest", which neofetch does not know; the patch takes the macOS
  code paths and names the system "SiliconForest <version> <build>";
- honest answers where the macOS paths would be wrong or need tools we do not have
  (`system_profiler`): DE and WM are Chonkstep, the theme comes from the desk's state file, the model
  from `hw.targettype`, the GPU from the CPU brand string, the panel size for J293.

Install (the M1 has no sftp subsystem, so the file goes over `ssh … 'cat >'`):

    patch -o neofetch /path/to/neofetch-7.1.0 neofetch-7.1.0-siliconforest.patch
    # shebang is /opt/homebrew/bin/bash (bash 5 from Homebrew; /bin/sh is not bash here)
    ssh root@<m1> 'cat > /usr/local/bin/neofetch && chmod 755 /usr/local/bin/neofetch' < neofetch

Installed on the M1 on 17 November 2026 as `/usr/local/bin/neofetch` (sha256 `f929f5aa7a93…`).
