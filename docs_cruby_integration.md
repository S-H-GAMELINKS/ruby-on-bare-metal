# CRuby 統合メモ

## このサンプルでまだやっていないこと

- CRuby 本体のビルド
- `ruby_init()` 呼び出し
- `rb_eval_string_protect()` 呼び出し
- 例外表示
- `require` 経路

## 最初に試す方針

1. ホスト上で `--disable-gems` な最小構成を確認
2. 必須依存関数を洗い出す
3. `ruby_on_bare_metal_*` API に寄せて互換層を作る
4. `compat/ruby_on_bare_metal_compat.c` の `ruby_on_bare_metal_cruby_demo()` を本物の CRuby 初期化コードへ置き換える

## 互換層にまとめたいもの

- メモリアロケータ
- 標準出力/標準エラー
- 時刻取得
- 埋め込みファイル読み込み
- process/thread/signal のスタブ
