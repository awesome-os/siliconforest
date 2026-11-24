# SPDX-License-Identifier: MIT
# Patch a frozen installed QuartzCore only for an owned private qualification.
# rendererPatch=null MUST reproduce the original drvPath. No moving UI source.
{originalJSON, rendererPatch ? null, extensionHeader ? ./quartzcore-nv12-copy.h}:
let
  document=builtins.fromJSON (builtins.readFile originalJSON);
  entries=builtins.attrValues document.derivations;
  raw=builtins.head entries;
  clean=builtins.unsafeDiscardStringContext;
  header=if rendererPatch==null then null else clean (toString
    (builtins.path {path=extensionHeader;name="quartzcore-nv12-copy.h";}));
  patch=if rendererPatch==null then null else clean (toString
    (builtins.path {path=rendererPatch;name="quartzcore-nv12-copy.patch";}));
  sources=map (name: "/nix/store/"+name) raw.inputs.srcs
    ++(if rendererPatch==null then [] else [header patch]);
  context=builtins.listToAttrs (
    map (name: {name="/nix/store/"+name;value={outputs=raw.inputs.drvs.${name}.outputs;};})
        (builtins.attrNames raw.inputs.drvs)
    ++map (name: {inherit name;value={path=true;};}) sources);
  env=builtins.mapAttrs (_: value: clean value) (builtins.removeAttrs raw.env ["out"]);
  addition=if rendererPatch==null then {} else {
    postPatch=builtins.appendContext ((env.postPatch or "")+''
      cp ${header} quartzcore-nv12-copy.h
      patch --fuzz=0 -p1 < ${patch}
    '') context;
  };
in assert builtins.length entries==1;
   assert raw.env.__structuredAttrs=="" && builtins.attrNames raw.outputs==["out"];
   builtins.derivation (env//addition//{
     builder=clean raw.builder;system=clean raw.system;args=map clean raw.args;
     outputs=builtins.attrNames raw.outputs;__structuredAttrs=false;
     buildPhase=builtins.appendContext env.buildPhase context;
   })
