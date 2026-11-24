# SPDX-License-Identifier: MIT
let e = import ../environment.nix;
in import ../objc-framework.nix {
  name = "CloudKit";
  arc = false;
  extraIncludes = "-fobjc-weak";
  patches = [ ./patches/0001-own-declarations-manual-accessors.patch ./patches/0002-ascii-name-validation.patch ];
  prepare = ''
    cp ${./getter-aliases.m} userland/CloudKit/SFGetterAliases.m
    ${e.pkgs.python3}/bin/python3 userland/CloudKit/generate-properties.py ${./include/CloudKit} userland/CloudKit/Properties.m
  '';
}
