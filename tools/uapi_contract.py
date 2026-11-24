#!/usr/bin/env python3
"""Compare explicit C enum values or generate a reviewed C layout probe.

Enum agreement does not imply equal structs, ioctl encodings, semantics, or
binary compatibility. Generated probes must be compiled separately with each
real SDK. This tool does not execute compilers or downloaded code.
"""
from __future__ import annotations
import argparse
import ast
import json
from pathlib import Path
import re
import sys
from typing import Any
try:
    from .source_inventory import mask_noncode, read_source, SourceError
    from .lab import read_json
except ImportError:
    from source_inventory import mask_noncode, read_source, SourceError
    from lab import read_json

IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")

def integer_expression(text: str, symbols: dict[str, int]) -> int:
    if len(text) > 4096: raise SourceError("Enum expression too large")
    text = re.sub(r"\b(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]+\b", r"\1", text)
    try: root = ast.parse(text.strip(), mode="eval").body
    except (SyntaxError, ValueError) as exc: raise SourceError("Unsupported C enum expression") from exc
    if sum(1 for _ in ast.walk(root)) > 256: raise SourceError("Enum expression too complex")
    def calculate(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and type(node.value) is int: value = node.value
        elif isinstance(node, ast.Name) and node.id in symbols: value = symbols[node.id]
        elif isinstance(node, ast.UnaryOp):
            operand = calculate(node.operand)
            if isinstance(node.op, ast.UAdd): value = operand
            elif isinstance(node.op, ast.USub): value = -operand
            elif isinstance(node.op, ast.Invert): value = ~operand
            else: raise SourceError("Unsupported unary operator")
        elif isinstance(node, ast.BinOp):
            a, b = calculate(node.left), calculate(node.right)
            if isinstance(node.op, (ast.LShift, ast.RShift)) and not 0 <= b <= 63:
                raise SourceError("Shift outside supported range")
            if isinstance(node.op, ast.Add): value=a+b
            elif isinstance(node.op, ast.Sub): value=a-b
            elif isinstance(node.op, ast.Mult): value=a*b
            elif isinstance(node.op, ast.BitOr): value=a|b
            elif isinstance(node.op, ast.BitAnd): value=a&b
            elif isinstance(node.op, ast.BitXor): value=a^b
            elif isinstance(node.op, ast.LShift): value=a<<b
            elif isinstance(node.op, ast.RShift): value=a>>b
            else: raise SourceError("Unsupported binary operator")
        else: raise SourceError("Unresolved or unsafe enum expression")
        if not -(1<<64) <= value < (1<<64): raise SourceError("Enum expression outside 64-bit bound")
        return value
    return calculate(root)

def parse_enum(text: str, enum_name: str) -> dict[str, int]:
    if not IDENT.fullmatch(enum_name): raise SourceError("Invalid enum name")
    code = mask_noncode(text)
    matches = list(re.finditer(r"\benum\s+" + re.escape(enum_name) + r"\s*\{([^{}]*)\}\s*;", code))
    if len(matches) != 1: raise SourceError("Need exactly one complete enum definition")
    body = matches[0].group(1)
    if "#" in body: raise SourceError("Conditional enum requires preprocessing first")
    result: dict[str, int] = {}
    current = -1
    for entry in body.split(","):
        if not entry.strip(): continue
        pieces = entry.strip().split("=",1)
        name = pieces[0].strip()
        if not IDENT.fullmatch(name) or name in result: raise SourceError("Invalid or duplicate enumerator")
        current = integer_expression(pieces[1],result) if len(pieces)==2 else current+1
        if not -(1<<64) <= current < (1<<64): raise SourceError("Enum auto-increment overflow")
        result[name]=current
    if not result: raise SourceError("Empty enum")
    return result

def compare_enum(left: str, right: str, name: str) -> dict[str, Any]:
    a, b = parse_enum(left,name), parse_enum(right,name)
    differences = [{"name": key, "left": a.get(key), "right": b.get(key)}
                   for key in sorted(a.keys() | b.keys()) if a.get(key)!=b.get(key)]
    return {"status": "DIFFERENT_ENUM_VALUES" if differences else "MATCHED_ENUM_VALUES",
            "enum":name,"left":a,"right":b,"differences":differences,
            "binary_abi_proven":False,
            "not_checked":["struct layout","ioctl encodings","feature bits","runtime behavior","firmware ABI"]}

def ident(value: Any) -> str:
    if not isinstance(value,str) or not IDENT.fullmatch(value): raise SourceError(f"Invalid C identifier: {value!r}")
    return value

def generate_probe(spec: dict[str, Any]) -> str:
    header=spec.get("header")
    if not isinstance(header,str) or not re.fullmatch(r"[A-Za-z0-9_./-]+",header) or any(p in {"", ".", ".."} for p in header.split("/")):
        raise SourceError("Invalid include path")
    tag=spec.get("abi_tag")
    if not isinstance(tag,str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,80}",tag): raise SourceError("Invalid ABI label")
    structs=spec.get("structs"); constants=spec.get("constants",[])
    if not isinstance(structs,list) or not structs: raise SourceError("Missing struct specification")
    if not isinstance(constants,list): raise SourceError("constants must be a list")
    lines=['/* Generated layout probe. Compile separately against each real SDK. */',
           '#include <stddef.h>', '#include <stdio.h>', '#include <inttypes.h>',
           f'#include "{header}"', 'int main(void) {']
    def literal(value: str) -> None:
        lines.append('  fputs('+json.dumps(value)+', stdout);')
    literal('{"schema_version":1,"abi_tag":'+json.dumps(tag)+',"structs":{')
    seen=set()
    for i,item in enumerate(structs):
        if not isinstance(item,dict): raise SourceError("Struct entry must be an object")
        name=ident(item.get("name")); fields=item.get("fields")
        if name in seen: raise SourceError("Duplicate struct")
        seen.add(name)
        if not isinstance(fields,list) or not all(isinstance(f,str) for f in fields): raise SourceError("Invalid fields")
        if len(set(fields))!=len(fields): raise SourceError("Duplicate fields")
        if i: literal(',')
        literal(json.dumps(name)+':{"size":')
        lines.append(f'  printf("%zu", sizeof(struct {name}));')
        literal(',"align":'); lines.append(f'  printf("%zu", _Alignof(struct {name}));')
        literal(',"offsets":{')
        for j,field in enumerate(fields):
            field=ident(field)
            literal((',' if j else '')+json.dumps(field)+':')
            lines.append(f'  printf("%zu", offsetof(struct {name}, {field}));')
        literal('}}')
    literal('},"constants":{')
    seen=set()
    for i,value in enumerate(constants):
        value=ident(value)
        if value in seen: raise SourceError("Duplicate constant")
        seen.add(value)
        literal((',' if i else '')+json.dumps(value)+':')
        lines.append(f'  printf("%" PRIu64, (uint64_t)({value}));')
    literal('}}\n'); lines.extend(['  return ferror(stdout) ? 1 : 0;', '}',''])
    return '\n'.join(lines)

def compare_layouts(a: dict[str, Any], b: dict[str, Any]) -> dict[str, Any]:
    for doc in (a,b):
        if doc.get("schema_version")!=1 or not isinstance(doc.get("abi_tag"),str): raise SourceError("Invalid probe report")
        if not isinstance(doc.get("structs"),dict) or not isinstance(doc.get("constants"),dict): raise SourceError("Incomplete probe report")
        if not doc["structs"]: raise SourceError("Empty layout report")
        for name,value in doc["structs"].items():
            ident(name)
            if not isinstance(value,dict) or set(value)!={"size","align","offsets"}: raise SourceError("Invalid struct result")
            for key in ("size","align"):
                if type(value[key]) is not int or value[key]<=0: raise SourceError("Invalid size/alignment")
            if not isinstance(value["offsets"],dict): raise SourceError("Invalid offset map")
            for field,offset in value["offsets"].items():
                ident(field)
                if type(offset) is not int or not 0<=offset<value["size"]: raise SourceError("Invalid field offset")
        for name,value in doc["constants"].items():
            ident(name)
            if type(value) is not int or not 0<=value<(1<<64): raise SourceError("Invalid constant result")
    if a["abi_tag"]!=b["abi_tag"]: raise SourceError("ABI labels differ; review target identities before comparison")
    differences=[]
    for section in ("structs","constants"):
        for name in sorted(a[section].keys() | b[section].keys()):
            if a[section].get(name)!=b[section].get(name):
                differences.append({"section":section,"name":name,"left":a[section].get(name),"right":b[section].get(name)})
    return {"status":"DIFFERENT_LAYOUTS" if differences else "MATCHED_LAYOUTS",
            "differences":differences,"runtime_compatibility_proven":False,
            "limits":"Compares supplied reports only. Targets, compiler settings, and capture authenticity require independent verification."}

def main(argv: list[str] | None = None) -> int:
    p=argparse.ArgumentParser(description=__doc__); sub=p.add_subparsers(dest="command",required=True)
    enum=sub.add_parser("enum"); enum.add_argument("left",type=Path); enum.add_argument("right",type=Path)
    enum.add_argument("--name",default="drm_asahi_ioctl_id")
    probe=sub.add_parser("generate-probe"); probe.add_argument("spec",type=Path)
    layout=sub.add_parser("compare-layouts"); layout.add_argument("left",type=Path); layout.add_argument("right",type=Path)
    args=p.parse_args(argv)
    try:
        if args.command=="generate-probe": print(generate_probe(read_json(args.spec)),end=""); return 0
        if args.command=="enum": result=compare_enum(read_source(args.left),read_source(args.right),args.name)
        else: result=compare_layouts(read_json(args.left),read_json(args.right))
        print(json.dumps(result,indent=2)); return 1 if result["differences"] else 0
    except (ValueError,OSError,UnicodeError,TypeError) as exc:
        print(json.dumps({"status":"INPUT_ERROR","error":str(exc)}),file=sys.stderr); return 2
if __name__=="__main__": raise SystemExit(main())
