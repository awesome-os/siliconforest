# SPDX-License-Identifier: MIT
# Unmodified official Noto data, redistributed under SIL OFL 1.1.
{ pkgs }:
let root="https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac";
in {
 font=pkgs.fetchurl {
  url="${root}/hinted/ttf/NotoSansDevanagari/NotoSansDevanagari-Regular.ttf";
  sha256="385e78e6359a9d88a0f243d53b1209d7548361ba2194e2b9ec779bcaa7e8949d";
 };
 thai=pkgs.fetchurl {
  url="${root}/hinted/ttf/NotoSansThai/NotoSansThai-Regular.ttf";
  sha256="404ddfb5ed0aaa6b6ec8a85700d682978992062d67da93903967b56cbd9a4acc";
 };
 license=pkgs.fetchurl {
  url="${root}/LICENSE";
  sha256="0dab92d0544f7b233403f14b84a663bdbfa746982eda629e7f4f9ffe1b036feb";
 };
}
