Finch AppleScript language engine from ASValues.m, ASParser.m, ASInterpreter.m
and their original headers. Linux/Nix build against open SiliconForest headers;
Finch licenses retained. This is a staged engine, not OpenScripting.framework.
The full OSA.m facade and osascript need CoreServices Apple-event descriptors
and component registration absent from our current root. Do not install a
partial framework or replace Carbon/CoreServices.

The independent CLI probe covers arithmetic, loops, handlers, lists, records,
conditionals, text concatenation and invalid syntax. It invokes no application,
GUI, shell, file or other standard-addition commands; those source paths remain
untested. Build and runtime outcomes belong in SF-AB's handover table.

Current build FAIL: SiliconForest lacks declarations for string enumeration/composed
character methods, calendar time-setting, file display names and NSDataDetector.
The probe has not run. No runtime support is claimed; the locale initializer
adaptation alone does not supply these prerequisites.
