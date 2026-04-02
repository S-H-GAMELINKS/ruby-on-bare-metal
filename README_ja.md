# Ruby on Bare Metal

> ベアメタル x86_64 で CRuby を動かす小型システム

[English version](README.md)

Ruby on Bare Metal は、CRuby をベアメタルの x86_64 ハードウェア上で直接動作させる小型システムです。Linux 互換 OS を作るのではなく、**CRuby が生きられる最小世界を実装**し、その上に Ruby ネイティブなユーザーランドを構築しています。

またVim ライクな Ruby 製テキストエディタ [mui](https://github.com/S-H-GAMELINKS/mui) がこのカーネル上で動作します。

### 主な数値

| | |
|---|---|
| カーネルイメージサイズ | 4.4 MB（CRuby + musl + mui 含む） |
| Ruby バージョン | CRuby 4.0.2 |
| ターゲット | x86_64（QEMU q35） |
| カーネル C コード | 約 1,000 行 |
| 実装した syscall 数 | 83 |
| 埋め込み Ruby ファイル | 97（mui）+ 2（init/hello） |

## 必要な環境

**OS**: Linux（Ubuntu/Debian 推奨、WSL2 対応）

必要なパッケージをインストールします:

```bash
sudo apt-get install -y \
  clang lld llvm \
  qemu-system-x86 \
  ruby \
  git make autoconf
```

## クイックスタート

```bash
# 1. 依存ライブラリのクローンとビルド
make setup

# 2. カーネルをビルド
make

# 3. QEMU で実行
make run
```

便利スクリプトも使えます:

```bash
./setup.sh   # 'make setup && make' と同等
make run
```

以下のような出力が表示されます:

```
boot ok
timer ok
tls ok
memory ok
cruby init ok
```

続いて Ruby シェルプロンプトが起動します。

## ビルドターゲット

| ターゲット | 説明 |
|---|---|
| `make setup` | サードパーティ依存のクローン・パッチ・ビルド |
| `make` | `build/kernel.elf` のビルド（デフォルト） |
| `make run` | QEMU で起動（512MB RAM、シリアルを stdio に接続） |
| `make clean` | ビルド成果物の削除 |
| `make distclean` | ビルド成果物とサードパーティソースの削除 |

### `make setup` の内部動作

`make setup` は以下のステージを順番に実行します:

1. **`setup-clone`** — CRuby v4.0.2、musl libc、mui を各上流リポジトリから `third_party/` にクローン
2. **`setup-patch`** — `patches/` のパッチを適用（musl の syscall リダイレクト、CRuby のスレッド修正、mui のターミナルアダプタ）
3. **`setup-musl`** — musl libc を静的ライブラリとしてビルドし、Ruby on Bare Metal 独自実装で置き換える malloc/pthread オブジェクトを除去
4. **`setup-cruby-host`** — ホスト上で miniruby をビルド（クロスビルド時のコード生成に必要）
5. **`setup-cruby-cross`** — CRuby をベアメタルターゲット向けに `libruby-static.a` としてクロスコンパイル

各ステージは冪等です。`make setup` を再実行すると、完了済みのステップはスキップされます。

## プロジェクト構成

```
.
├── Makefile                  # ビルド全体の管理
├── setup.sh                  # セットアップ用ラッパースクリプト
├── boot/
│   ├── boot.S                # 32-bit Multiboot ブートローダー（→ Long Mode）
│   └── linker.ld             # 32-bit リンカスクリプト
├── kernel/
│   ├── kernel_main.c         # カーネルエントリポイント
│   ├── entry64.S             # 64-bit アセンブリエントリ
│   ├── kernel64.ld           # 64-bit リンカスクリプト
│   ├── serial.c              # COM1 シリアル I/O ドライバ
│   ├── timer.c               # TSC タイマー（PIT 較正付き）
│   ├── memory.c              # バンプアロケータ
│   ├── panic.c               # カーネルパニックハンドラ
│   ├── embedded_files.c      # 埋め込みファイルシステムバックエンド
│   └── kernel.h              # カーネル API ヘッダ
├── compat/                   # CRuby 互換層
│   ├── ruby_on_bare_metal_compat.c       # CRuby VM 初期化
│   ├── ruby_on_bare_metal_syscall.c      # Linux syscall エミュレーション（83 syscall）
│   ├── ruby_on_bare_metal_malloc.c       # カスタムメモリアロケータ
│   ├── ruby_on_bare_metal_pthread.c      # シングルスレッド用 pthread スタブ
│   └── ruby_on_bare_metal_enc.c          # 静的エンコーディング初期化
├── cruby_build/              # CRuby クロスコンパイル設定
│   ├── Makefile              # クロスビルドルール
│   └── include/ruby/config.h # Ruby on Bare Metal 向け CRuby 設定
├── ruby/scripts/             # 埋め込み Ruby スクリプト
│   ├── init.rb               # メイン初期化（シェル、VFS、require）
│   └── hello.rb              # Hello world テスト
├── tools/                    # ビルドユーティリティ
│   ├── embed_scripts.rb      # .rb ファイルを C バイト配列に変換
│   └── embed_mui.rb          # mui ライブラリの埋め込み
├── patches/                  # サードパーティ用パッチ
│   ├── cruby.patch           # CRuby のベアメタル対応修正
│   ├── musl.patch            # musl の syscall リダイレクト
│   └── mui.patch             # mui のベアメタル対応
└── third_party/              # 外部ソース（make setup で作成）
    ├── cruby/                # CRuby v4.0.2
    ├── musl/                 # musl libc
    └── mui/                  # mui エディタ
```

## アーキテクチャ

```
┌─────────────────────────────────────────────┐
│ mui エディタ（97 Ruby ファイル、Vim 風）      │
│ Ruby シェル / REPL（init.rb）                │
├─────────────────────────────────────────────┤
│ VFS（Ruby Hash）│  require オーバーライド     │
├─────────────────────────────────────────────┤
│ CRuby 4.0.2 VM（libruby-static.a）          │
│  + Prism パーサー + GC + 正規表現（鬼車）     │
├─────────────────────────────────────────────┤
│ musl libc（libc_ruby_on_bare_metal.a）       │
│  syscall 命令 → ruby_on_bare_metal_syscall() に置換 │
├─────────────────────────────────────────────┤
│ Ruby on Bare Metal カーネル（C 約 1,000 行）  │
│  serial / timer / memory / syscall / VFS     │
├─────────────────────────────────────────────┤
│ ブート（Multiboot → Long Mode → 64-bit）     │
├─────────────────────────────────────────────┤
│ QEMU（q35, 512MB RAM, serial stdio）         │
└─────────────────────────────────────────────┘
```

### ブートシーケンス

Ruby on Bare Metal は **二段構成のブートプロセス** を採用しています。QEMU の `-kernel` オプションが 32-bit Multiboot ELF のみを受け付ける一方、CRuby は 64-bit 環境を必要とするためです。

1. **Stage 1（32-bit）**: `boot/boot.S` — QEMU が Multiboot 経由でロード。ページテーブル設定（512MB のアイデンティティマッピング、2MB ページ）、SSE/FPU・PAE・Long Mode の有効化を行い、64-bit カーネルにジャンプ。
2. **Stage 2（64-bit）**: `kernel/entry64.S` → `kernel/kernel_main.c` — シリアル出力、タイマー、TLS、メモリアロケータを初期化し、CRuby VM を起動して埋め込みの `init.rb` を評価。

### Linux なしで CRuby が動く仕組み

musl libc は通常、Linux の `syscall` 命令を発行します。Ruby on Bare Metal では musl にパッチを当て、代わりに `ruby_on_bare_metal_syscall()`（C 関数）を呼び出すように変更しています。この関数がカーネル自身のシリアル I/O、メモリアロケータ、タイマー、埋め込みファイルシステムを使って 83 個の Linux syscall をエミュレートします。これにより、実際の Linux カーネルなしで CRuby に馴染みの libc インターフェースを提供しています。

## サードパーティ依存

| ライブラリ | バージョン | ライセンス | 役割 |
|---|---|---|---|
| [CRuby](https://github.com/ruby/ruby) | v4.0.2 | [Ruby License](https://www.ruby-lang.org/en/about/license.txt) / [BSD-2-Clause](https://opensource.org/licenses/BSD-2-Clause) / [GPL-2.0](https://www.gnu.org/licenses/gpl-2.0.html) | Ruby インタプリタ（静的ライブラリとしてコンパイル） |
| [musl libc](https://musl.libc.org/) | 最新 | [MIT](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT) | 最小 C 標準ライブラリ（syscall リダイレクト付き） |
| [mui](https://github.com/S-H-GAMELINKS/mui) | 最新 | [MIT](https://github.com/S-H-GAMELINKS/mui/blob/main/LICENSE) | Ruby 製 Vim ライクテキストエディタ |

すべての依存は `make setup` で自動クローンされます。ソースコードは本リポジトリには含まれていません。

## ツールチェイン

Ruby on Bare Metal は **LLVM ツールチェイン** のみを使用します:

| ツール | 用途 |
|---|---|
| `clang` | C コンパイラ |
| `ld.lld` | リンカ |
| `llvm-ar` | アーカイブツール |
| `llvm-objcopy` | ELF → フラットバイナリ変換 |

ランタイムサポート関数として LLVM compiler-rt ビルトインライブラリ（`libclang_rt.builtins-x86_64.a`）もリンクされます。

## トラブルシューティング

### `make setup` で `autoconf: command not found`

CRuby の configure スクリプト生成に autoconf が必要です:

```bash
sudo apt-get install -y autoconf
```

### `ruby: command not found`

ビルドツール（`tools/embed_scripts.rb` 等）にホスト Ruby が必要です:

```bash
sudo apt-get install -y ruby
```

### QEMU の出力が表示されない

`-serial stdio` が指定されていることを確認してください（Makefile ではデフォルトで設定済み）。GUI 環境で実行している場合、`-display none` もデフォルトで設定されており、空の QEMU ウィンドウが開くのを防ぎます。

### リンカエラーでビルドが失敗する

`make setup` が正常に完了しているか確認してください。不確かな場合は `make distclean && make setup` で依存をすべて再ビルドします。

## 開発

開発の詳細な記録（遭遇したバグとその解決方法を含む）は [DEVELOPMENT_LOG.md](../DEVELOPMENT_LOG.md) を参照してください。

開発で修正する主なファイル:

| 作業 | ファイル |
|---|---|
| syscall の追加 | `compat/ruby_on_bare_metal_syscall.c` |
| メモリレイアウトの変更 | `compat/ruby_on_bare_metal_malloc.c`, `boot/boot.S` |
| デバイスドライバの追加 | `kernel/serial.c`, `kernel/timer.c` |
| ブートシーケンスの変更 | `kernel/kernel_main.c`, `kernel/entry64.S` |
| Ruby スクリプトの追加 | `ruby/scripts/`, `tools/embed_scripts.rb` |
| CRuby 設定の変更 | `cruby_build/include/ruby/config.h` |

## ライセンス

本プロジェクトは MIT License のもとで公開されています。詳細は [LICENSE](LICENSE) ファイルを参照してください。

本リポジトリにはオリジナルコードとパッチファイルのみが含まれています。サードパーティの依存（CRuby、musl libc、mui）のソースコードは含まれておらず、`make setup` によりビルド時にダウンロードされます。カーネルイメージをビルドすると、これらのライブラリが静的リンクされます。ビルド成果物を再配布する場合は、各依存のライセンス条件を確認してください。

