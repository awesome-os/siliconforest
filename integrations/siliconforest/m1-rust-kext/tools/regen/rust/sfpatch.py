import re, sys
ROOT = "work/"
def rd(f): return open(ROOT + f).read().split("\n")
def wr(f, l): open(ROOT + f, "w").write("\n".join(l))
def gate(f, pat, cfg, nth=0, all=False):
    """insert #[cfg(cfg)] above the item whose line matches pat (above its docs/attributes)"""
    l = rd(f); hits = [i for i, x in enumerate(l) if re.search(pat, x)]
    assert hits, (f, pat)
    for i in reversed(hits if all else [hits[nth]]):
        j = i
        while j > 0 and re.match(r"\s*(///|#\[|//!?\s)", l[j - 1]): j -= 1
        ind = re.match(r"\s*", l[i]).group(0)
        if l[j].strip() == f"#[cfg({cfg})]": continue
        l.insert(j, f"{ind}#[cfg({cfg})]")
    wr(f, l)
def unuse(f, line, cfg, use):
    """drop one line of a `use crate::{...}` group and re-add it as its own gated use"""
    l = rd(f); i = next(i for i, x in enumerate(l) if x.strip() == line.strip())
    del l[i]
    j = next(k for k in range(i, len(l)) if l[k].startswith("};"))
    l[j + 1:j + 1] = [f"#[cfg({cfg})]", use]
    wr(f, l)
def sub(f, old, new, count=1):
    s = open(ROOT + f).read(); assert s.count(old) >= 1, (f, old)
    open(ROOT + f, "w").write(s.replace(old, new, count))
