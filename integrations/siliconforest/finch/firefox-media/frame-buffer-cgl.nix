# SPDX-License-Identifier: MIT
# Reconstruct one original Linux/Nix derivation without reevaluating moving UI
# sources. With bufferedHeader=null, drvPath MUST equal the supplied original.
# The private diagnostic changes only the observer source input and postPatch.
{ originalJSON, originalHeader, bufferedHeader ? null }:
let
  document=builtins.fromJSON (builtins.readFile originalJSON);
  entries=builtins.attrValues document.derivations;
  raw=builtins.head entries;
  clean=builtins.unsafeDiscardStringContext;
  old=clean (toString originalHeader);
  header=if bufferedHeader==null then old else clean (toString bufferedHeader);
  anchor="cp ${old} src/Frameworks/OpenGL/sffx-frame-id-export.h";
  replacement="cp ${header} src/Frameworks/OpenGL/sffx-frame-id-export.h";
  sources=map (name: "/nix/store/"+name) raw.inputs.srcs;
  replacedSources=map (source: if source==old then header else source) sources;
  context=builtins.listToAttrs (
    map (name: {name="/nix/store/"+name;value={outputs=raw.inputs.drvs.${name}.outputs;};})
        (builtins.attrNames raw.inputs.drvs)
    ++map (name: {inherit name;value={path=true;};}) replacedSources);
  env=builtins.mapAttrs (_: value: clean value) (builtins.removeAttrs raw.env ["out"]);
  postPatch=builtins.replaceStrings [anchor] [replacement] env.postPatch;
in assert builtins.length entries==1;
   assert raw.env.__structuredAttrs=="" && builtins.attrNames raw.outputs==["out"];
   assert builtins.elem old sources;
   assert builtins.length (builtins.split anchor env.postPatch)==3;
   builtins.derivation (env//{
     builder=clean raw.builder;system=clean raw.system;args=map clean raw.args;
     outputs=builtins.attrNames raw.outputs;__structuredAttrs=false;
     postPatch=builtins.appendContext postPatch context;
   })
