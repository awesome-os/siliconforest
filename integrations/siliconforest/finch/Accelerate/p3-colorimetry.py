#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent public-colorimetry calculation, not an ICC/CMM conformance test."""
import json
import math


def dot(a,b):return sum(x*y for x,y in zip(a,b))
def transpose(a):return list(map(list,zip(*a)))
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def inverse(a):
    columns=[cross(a[1],a[2]),cross(a[2],a[0]),cross(a[0],a[1])]
    determinant=dot(a[0],columns[0])
    return [[v/determinant for v in row] for row in transpose(columns)]
def vector(a,b):return [dot(row,b) for row in a]
def matrix(a,b):return [[dot(row,col) for col in transpose(b)] for row in a]
def rgb_to_xyz(primaries):
    xyz=transpose([[x/y,1,(1-x-y)/y] for x,y in primaries])
    x,y=.3127,.3290
    scale=vector(inverse(xyz),[x/y,1,(1-x-y)/y])
    return [[v*scale[col] for col,v in enumerate(row)] for row in xyz]
def decode(v):return v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
def encode(v):return 12.92*v if v<=.0031308 else 1.055*v**(1/2.4)-.055


def main():
    transform=matrix(inverse(rgb_to_xyz([(.68,.32),(.265,.69),(.15,.06)])),
                     rgb_to_xyz([(.64,.33),(.30,.60),(.15,.06)]))
    seed=12345;source=[]
    for _ in range(216*5):
        seed=(seed*1664525+1013904223)&0xffffffff;source.append((seed>>8)&255)
    identity=converted=0
    for y in range(5):
        for x in range(13):
            rgb=source[y*216+4*x:y*216+4*x+3]
            identity+=sum(rgb)+255
            p3=[encode(v) for v in vector(transform,[decode(v/255) for v in rgb])]
            converted+=sum(math.floor(min(1,max(0,v))*255+.5) for v in p3)+255
    coarse=lambda total:math.floor(total/64+.5)//8
    print(json.dumps(dict(identity_sum=identity,identity_coarse=coarse(identity),
        ideal_p3_sum=converted,ideal_p3_coarse=coarse(converted),srgb_to_p3_linear=transform,
        limits='Independent RGB colorimetry with nearest U8 rounding, not execution or formal ICC/CMM qualification.'),indent=2))


if __name__=='__main__':main()
