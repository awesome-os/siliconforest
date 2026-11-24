# SPDX-License-Identifier: MIT
"""Smoke tests for poured bottles: one real use of each program, run inside the guest.

Each entry: (test name, formula, seconds allowed, shell script, regular expression the
output must contain, kind). kind 'real' exercises the program's function; 'version' only
proves that it starts (used where the function needs a terminal or tools we do not have,
and said so in the report). The script runs in an empty directory with
PATH=/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/sbin:/usr/bin:/usr/sbin and HOME=/tmp/hbhome.
A test passes when the script exits 0 and the expression matches.
"""
O = '/opt/homebrew/opt'

TESTS = [
    ('jq', 'jq', 240, r'''echo '{"a":[1,2,{"b":"x"}]}' | jq -c '.a[2].b, (.a|length)' ''', r'"x"\s+3', 'real'),
    ('tree', 'tree', 240, 'mkdir -p t/a/b && touch t/a/b/f && tree t', r'3 directories, 1 file', 'real'),
    ('gnu-sed', 'gnu-sed', 240, r'''echo hello | gsed -E 's/(l+)/[\1]/' ''', r'he\[ll\]o', 'real'),
    ('grep', 'grep', 240, r'''printf 'a\nb1\nc\n' | ggrep -P 'b\d' ''', r'^b1', 'real'),
    ('findutils', 'findutils', 240, 'mkdir -p t/a && touch t/a/x.txt t/y.txt && gfind t -type f -name "*.txt" | gxargs -n1 basename | sort | tr "\\n" " "', r'x\.txt y\.txt', 'real'),
    ('make', 'make', 240, r'''printf 'all: ; @echo made-$(words a b c)\n' > Makefile && gmake''', r'made-3', 'real'),
    ('xz', 'xz', 240, 'echo hello-xz | xz -9 | xz -d', r'hello-xz', 'real'),
    ('zstd', 'zstd', 240, 'echo hello-zstd | zstd -q -19 | zstd -dq', r'hello-zstd', 'real'),
    ('less', 'less', 240, r'''printf 'x\ny\n' | less; less -V | head -1''', r'x\s+y\s+less \d+', 'real'),
    ('fzf', 'fzf', 240, r'''printf 'apple\nbanana\ncherry\n' | fzf -f nan''', r'banana', 'real'),
    ('ripgrep', 'ripgrep', 240, 'mkdir -p t/d && echo "a needle here" > t/d/f.txt && echo hay > t/g.txt && rg -n needle t', r'f\.txt:1:a needle here', 'real'),
    ('fd', 'fd', 240, 'mkdir -p t/d && touch t/d/f.txt t/g.md && fd -e txt . t', r't/d/f\.txt', 'real'),
    ('zoxide', 'zoxide', 240, 'mkdir -p /tmp/hbzox && zoxide add /tmp/hbzox && zoxide query hbzox', r'/tmp/hbzox', 'real'),
    ('eza', 'eza', 240, 'mkdir t && echo hi > t/file && eza -l --no-user --no-time t', r'file', 'real'),
    ('bat', 'bat', 240, r'''printf '{"k": 1}\n' > f.json && bat --color=always --style=plain f.json | wc -c; bat --color=never --style=plain f.json''', r'^\s*\d{2,}\s+\{"k": 1\}', 'real'),
    ('lua', 'lua', 240, r'''lua -e 'print(#("x"):rep(10), 2^10, string.format("%05.1f", math.pi))' ''', r'10\s+1024\.0\s+003\.1', 'real'),
    ('perl', 'perl', 240, r'''perl -MPOSIX -MList::Util=sum -e 'print sum(1..10), " ", floor(3.7), " ", sprintf("%vd", $^V), "\n"' ''', r'55 3 5\.\d+', 'real'),
    ('gawk', 'gawk', 240, r'''echo 'a b c' | gawk '{print $2, NF, strftime("%Y", 0, 1)}' ''', r'b 3 1970', 'real'),
    ('coreutils', 'coreutils', 240, r'''printf abc | gsha256sum | cut -c1-8; gdate -u -d @86400 +%F; gseq 4 | gpaste -sd+; gfactor 1001; gls -1 / | ghead -2 | gwc -l''', r'ba7816bf\s+1970-01-02\s+1\+2\+3\+4\s+1001: 7 11 13\s+2', 'real'),
    ('bash', 'bash', 240, r'''bash -c 'a=(1 2 3); declare -A m=([k]=v); echo ${#a[@]} $((2**10)) ${m[k]} ${BASH_VERSINFO[0]}' ''', r'3 1024 v 5', 'real'),
    ('zsh', 'zsh', 240, r'''/opt/homebrew/bin/zsh -fc 'print ${(U)${:-abc}} $(( 6 * 7 )) $ZSH_VERSION' ''', r'ABC 42 5\.9', 'real'),
    ('fish', 'fish', 240, r'''fish -N -c 'echo (math "6 * 7") (string upper abc) $version' ''', r'42 ABC \d', 'real'),
    ('ncurses', 'ncurses', 240, O + '/ncurses/bin/infocmp -1 xterm-256color | head -3; ' + O + '/ncurses/bin/tput -T xterm-256color colors', r'xterm-256color[\s\S]*256', 'real'),
    ('sqlite', 'sqlite', 240, O + r'''/sqlite/bin/sqlite3 t.db 'create table t(x); insert into t values (41),(1); select sum(x), sqlite_version() from t;' ''', r'42\|3\.\d+', 'real'),
    ('openssl@3', 'openssl@3', 240, 'printf abc | ' + O + '/openssl@3/bin/openssl dgst -sha256; ' + O + '/openssl@3/bin/openssl genpkey -algorithm ed25519 | ' + O + '/openssl@3/bin/openssl pkey -noout -text | head -1', r'ba7816bf8f01cfea[\s\S]*ED25519 Private-Key', 'real'),
    ('htop', 'htop', 240, 'htop --version', r'htop 3\.', 'version'),
    ('nano', 'nano', 240, 'nano --version | head -1', r'GNU nano', 'version'),
    ('tmux', 'tmux', 240, r'''export LC_ALL=en_US.UTF-8; tmux -V && tmux new-session -d -s hb 'sleep 20' && tmux list-sessions && tmux kill-server''', r'tmux 3\.[\s\S]*hb: 1 windows', 'real'),
    ('vim', 'vim', 240, r'''echo 'aaa' > f && vim -es -u NONE -c '%s/a/b/g' -c 'wq' f; cat f; vim --version | head -1''', r'bbb\s+VIM - Vi IMproved 9', 'real'),
    ('neovim', 'neovim', 240, r'''nvim --headless -u NONE -c 'lua io.stdout:write(tostring(vim.fn.strlen("abc") + 39) .. "\n")' -c q''', r'42', 'real'),
    ('git', 'git', 300, r'''git init -q r && cd r && git config user.email t@example.invalid && git config user.name t && echo hi > f && git add f && git commit -q -m one && echo more >> f && git commit -q -am two && git log --oneline | wc -l && git diff HEAD~1 --stat | tail -1''', r'2\s+1 file changed, 1 insertion', 'real'),
    ('curl-https', 'curl', 180, O + '/curl/bin/curl -sS -m 60 -o page https://example.com/ -w "%{http_code} %{ssl_verify_result}\\n" && grep -c "Example Domain" page', r'200 0\s+[1-9]', 'real'),
    ('wget-https', 'wget', 180, 'wget -q -T 60 -O page https://example.com/ && grep -c "Example Domain" page', r'^[1-9]', 'real'),
    ('gnupg', 'gnupg', 300, r'''export GNUPGHOME=$PWD/gh && mkdir -m 700 gh && echo secret-text > f && gpg --batch --pinentry-mode loopback --passphrase pw -o f.gpg --symmetric f && gpg --batch --pinentry-mode loopback --passphrase pw -d f.gpg 2>/dev/null''', r'secret-text', 'real'),
    ('nmap', 'nmap', 300, 'nmap -sT -Pn -p 80 --host-timeout 60 example.com', r'80/tcp\s+open', 'real'),
    ('cmake', 'cmake', 180, r'''printf 'math(EXPR x "6 * 7")\nstring(SHA256 h "abc")\nmessage("x=${x} h=${h}")\n' > s.cmake && cmake -P s.cmake 2>&1''', r'x=42 h=ba7816bf', 'real'),
    ('gh', 'gh', 240, 'gh --version | head -1 && gh completion -s bash | head -1', r'gh version 2\.[\s\S]*bash completion', 'version'),
    ('starship', 'starship', 240, 'starship --version | head -1 && STARSHIP_CONFIG=/dev/null starship module character', r'starship 1\.', 'real'),
    ('python', 'python@3.14', 300, O + r'''/python@3.14/bin/python3.14 -c 'import sys, json, hashlib, sqlite3, ssl, zlib, ctypes, decimal; print(sys.version_info[:2], hashlib.sha256(b"abc").hexdigest()[:8], sqlite3.connect(":memory:").execute("select 6*7").fetchone()[0], zlib.crc32(b"a"), ctypes.sizeof(ctypes.c_void_p), ssl.OPENSSL_VERSION.split()[0], json.dumps({"k": [1]}))' ''', r'\(3, 14\) ba7816bf 42 3904355907 8 OpenSSL \{"k": \[1\]\}', 'real'),
    ('ruby', 'ruby', 300, O + r'''/ruby/bin/ruby -rjson -rdigest -e 'puts [1,2,3].sum, JSON.generate({a: 1}), Digest::SHA256.hexdigest("abc")[0,8], RUBY_VERSION' ''', r'6\s+\{"a":1\}\s+ba7816bf\s+4\.', 'real'),
    ('node', 'node', 900, r'''node -e 'console.log([1,2,3].map(x=>x*2).join(","), require("crypto").createHash("sha256").update("abc").digest("hex").slice(0,8), process.arch, require("zlib").gzipSync("a").length > 0)' ''', r'2,4,6 ba7816bf arm64 true', 'real'),
    ('imagemagick', 'imagemagick', 300, 'magick -size 32x24 gradient:red-blue out.png && magick identify out.png', r'PNG 32x24', 'real'),
    ('ffmpeg', 'ffmpeg', 600, 'ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc=d=1:s=64x64:r=5 -c:v libx264 out.mp4 && ffprobe -v error -show_entries stream=codec_name,width,nb_frames -of csv=p=0 out.mp4', r'h264,64,5', 'real'),
    ('go-version', 'go', 240, 'go version', r'go version go1\.\d+', 'version'),
    ('go-run', 'go', 1500, r'''export GOCACHE=$PWD/cache GOFLAGS=-mod=mod GOTOOLCHAIN=local && printf 'package main\nimport ("fmt"; "crypto/sha256")\nfunc main() { fmt.Printf("%%x\\n", sha256.Sum256([]byte("abc"))) }\n' > h.go && go run h.go''', r'ba7816bf8f01cfea', 'real'),
    ('rust-version', 'rust', 240, 'rustc --version && cargo --version', r'rustc 1\.[\s\S]*cargo 1\.', 'version'),
    ('rust-rlib', 'rust', 900, r'''printf 'pub fn f(a: u32) -> u32 { a * 2 }\n' > l.rs && rustc --crate-type=rlib -O l.rs && ls -1 libl.rlib''', r'libl\.rlib', 'real'),
    ('gcc-version', 'gcc', 240, 'gcc-16 --version | head -1', r'gcc-16 \(Homebrew GCC', 'version'),
    ('gcc-asm', 'gcc', 900, r'''printf 'int f(int a) { return a * 2; }\n' > f.c && gcc-16 -O1 -S -o f.s f.c; echo "gcc rc=$?"; grep -c -e lsl -e add f.s ''', r'^[1-9]', 'real'),
    ('llvm-version', 'llvm', 300, O + '/llvm/bin/clang --version | head -1', r'clang version \d+', 'version'),
    ('llvm-ir', 'llvm', 900, r'''printf 'int f(int a) { return a * 2; }\n' > f.c && ''' + O + '/llvm/bin/clang -O1 -S -emit-llvm -o f.ll f.c && grep -c -e shl -e mul f.ll && ' + O + '/llvm/bin/llc -o f.s f.ll && grep -c "lsl" f.s', r'^[1-9]\s+[1-9]', 'real'),
]

BY_FORMULA = {}
for _t in TESTS:
    BY_FORMULA.setdefault(_t[1], []).append(_t[0])
