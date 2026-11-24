# SPDX-License-Identifier: MIT
# Append an isolated private extension to one frozen, already reviewed CGL.
# extensionHeader=null MUST reproduce original drvPath. No moving UI evaluation.
{originalJSON, extensionHeader ? null, replaceExistingHeader ? false}:
let
  document=builtins.fromJSON (builtins.readFile originalJSON);
  entries=builtins.attrValues document.derivations;
  raw=builtins.head entries;
  clean=builtins.unsafeDiscardStringContext;
  header=if extensionHeader==null then null else clean (toString
    (builtins.path {path=extensionHeader;name="strict-nv12-plane.h";}));
  sources=map (name: "/nix/store/"+name) raw.inputs.srcs
    ++(if header==null then [] else [header]);
  context=builtins.listToAttrs (
    map (name: {name="/nix/store/"+name;value={outputs=raw.inputs.drvs.${name}.outputs;};})
        (builtins.attrNames raw.inputs.drvs)
    ++map (name: {inherit name;value={path=true;};}) sources);
  env=builtins.mapAttrs (_: value: clean value) (builtins.removeAttrs raw.env ["out"]);
  postPatch=env.postPatch+(if header==null then "" else if replaceExistingHeader then ''
    chmod u+w src/Frameworks/OpenGL/sffx-strict-nv12-plane.h
    cp ${header} src/Frameworks/OpenGL/sffx-strict-nv12-plane.h
  '' else ''
    cp ${header} src/Frameworks/OpenGL/sffx-strict-nv12-plane.h
    cat >> src/Frameworks/OpenGL/CGL.c <<'SF_NV12_EXTENSION'
    #include "sffx-strict-nv12-plane.h"
    SF_NV12_EXTENSION
  '');
in assert builtins.length entries==1;
   assert !replaceExistingHeader || (header!=null && builtins.length
     (builtins.split "#include \"sffx-strict-nv12-plane[.]h\"" env.postPatch)==3);
   assert raw.env.__structuredAttrs=="" && builtins.attrNames raw.outputs==["out"];
   builtins.derivation (env//{
     builder=clean raw.builder;system=clean raw.system;args=map clean raw.args;
     outputs=builtins.attrNames raw.outputs;__structuredAttrs=false;
     postPatch=builtins.appendContext postPatch context;
   })
