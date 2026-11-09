# PhoneMonitor 1.1

DAWのインサートに挿したトラック（マスター推奨）の音を、ブラウザで聴くためのVST3エフェクトです。聴く側はアプリ不要です。

- **同じWi-Fiで聴く**：プラグイン画面のQR／URLをスマホで開く
- **遠くの人に聴かせる**：Cloudflareの一時URL（trycloudflare.com）を発行して、別の場所にいる人のブラウザに届ける

プラグイン自体は音を変えません（素通し）。音は16bit・ステレオ、DAWのサンプルレートのまま送ります（48kHzで1人あたり約1.5Mbps）。

## 遠くの人に聴かせる手順
1. 初回だけ「外部公開を開始」→「cloudflaredを取得」を押す（約30MB、`%APPDATA%\PhoneMonitor` に保存）
   - winget で入れた cloudflared や PATH 上の cloudflared.exe があれば、それを使います
2. 「外部公開を開始」を押すと、数秒で公開URLとQRが出る
3. 「URLをコピー」して相手に送る
4. 終わったら「外部公開を停止」（URLはその場で使えなくなる）

### 安全のための仕組み
- URLには毎回ランダムな鍵（`?k=…`）が付き、鍵が違うアクセスは拒否します
- 公開を開始するたびにURLと鍵が変わります。プロジェクトを開いても自動では公開しません
- 「メール認証」欄にアドレスを入れると、相手はそのメールに届くコードを入力してから聴けます（cloudflared 2026.10以降の `--allowed-mail`）。カンマ区切りで複数可、`*@example.com` でドメイン指定も可

### 注意
- 一時URL（Quick Tunnel）はCloudflareが検証用として提供しているもので、稼働保証はありません
- `%USERPROFILE%\.cloudflared\config.yaml` があると一時URLが使えません（一時的に名前を変える）
- DAWが強制終了した場合、cloudflared.exe が残ることがあります。タスクマネージャーで終了してください
- 通信は WebSocket です（一時URLは長時間のHTTPストリームに対応していないため）

## ビルド
GitHub Actions（`.github/workflows/build.yml`）で Windows 用 VST3 を生成します。`v1.1.0` のようなタグを付けて push すると Releases に zip が付きます。

再生ページは `web/page.html` を編集して `python tools/make_webpage.py` で `Source/WebPage.h` を再生成します。

## ライセンス
AGPLv3（JUCE 8）。QRコード生成に Nayuki QR-Code-generator（MIT）を使用。
