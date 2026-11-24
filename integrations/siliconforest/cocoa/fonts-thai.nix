# SPDX-License-Identifier: MIT
# TERM9 (FX-025): Thai for every application: Noto Sans Thai (regular, bold) and Noto Serif Thai, unmodified official
# Noto data under the SIL Open Font License 1.1 (the licence file beside them), for /System/Library/Fonts.
let
  a = import ./arm64.nix;
  root = "https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac";
  get = path: sha256: a.pkgs.fetchurl { url = "${root}/${path}"; inherit sha256; };
  sansRegular = get "hinted/ttf/NotoSansThai/NotoSansThai-Regular.ttf" "1k2akaynrdb776897nk75l3952cphbb00mx8r1p6pahaxnsxyka0";
  sansBold = get "hinted/ttf/NotoSansThai/NotoSansThai-Bold.ttf" "0lkw3m33ygd7d70my2jkgh59xirhx94fc2apr336ia1p4frcyrrb";
  serifRegular = get "hinted/ttf/NotoSerifThai/NotoSerifThai-Regular.ttf" "1q4a46x7yzpzczii7jr3x62l6ys8n5k1biqhc0dalnc4q41q6w9n";
  license = get "LICENSE" "0dab92d0544f7b233403f14b84a663bdbfa746982eda629e7f4f9ffe1b036feb";
in a.pkgs.runCommand "siliconforest-fonts-thai" { } ''
  d=$out/System/Library/Fonts/NotoThai
  mkdir -p $d
  cp ${sansRegular} $d/NotoSansThai-Regular.ttf
  cp ${sansBold} $d/NotoSansThai-Bold.ttf
  cp ${serifRegular} $d/NotoSerifThai-Regular.ttf
  cp ${license} $d/OFL.txt
''
