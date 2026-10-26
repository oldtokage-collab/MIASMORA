<p align="center"><img src="docs/MIASMORA_promo_1280x960.png" alt="MIASMORA — M5Stack Cardputer ADV 向けダークアンビエント・ドローンジェネレーター" width="100%"></p>

*[English version here / 英語版はこちら](README.md)*

**M5Stack Cardputer ADV** 向けの、**ダークアンビエント・ドローンジェネレーター**のファームウェアです。PlatformIO + Arduino フレームワークで開発しています。Claude に私のアイデアを伝え、コーディングしてもらっています。

MIASMORA は、ホラーゲームやホラー映画で流れるような、不穏で暗いドローンを鳴らし続けます。息づくような低音、互いにうなり合うデチューンの和音、ダクトを抜ける空気、錆びた金属、壊れたテレビ、上の階の足音、何かを引きずる音、這い回る何か。**すべてリアルタイムに合成していて、サンプル音源は一切使っていません。**

使い方は 3 つあります。

| | 使い方 | 主な機能 |
|---|---|---|
| **素材を作る** | ムードを作り込み、WAV に書き出して DAW で使う | 継ぎ目のないループ、ステム、バッチ、効果音のワンショット |
| **流しておく** | 部屋で鳴らしっぱなしにする | Drift・Arc で音がひとりでに移り変わり、Listen で部屋の物音に反応する。Cursed Mode で、ときどき奇妙なことが起きる |
| **遊ぶ** | 本体を傾けたり、隠したりする | PLANCHETTE（傾けて雰囲気を探る）、Poltergeist（隠しておくいたずら） |

穏やかな環境音を鳴らす **Ensō** の姉妹作です。Ensō の作りと、その開発で得た教訓を引き継いでいます。

**名前の由来。** MIASMORA（ミアズモーラ）は、*miasma*（瘴気。昔、病や不吉を運ぶと考えられた悪い空気）と *-ora* を合わせた名前です。

開発の様子は Reddit や私の [Twitter(X) アカウント](https://x.com/Tokagetchi)で公開・共有しています。

> **現状 100% バイブコーディングです！**
> AI の使用やバイブコーディングに抵抗がある方は申し訳ありません！

> **状態: v1.0（最初の正式公開版）**

---

## MIASMORA の主な特長

### 完全合成の 25 レイヤー

- **DRONE 11 種**（SUB, CLUSTER, AIR, METAL, STATIC, MACHINE, DISCORD, MICRO, VOX, SHEPARD, HEART）、**EVENTS 13 種**（PULSE, CREAK, SWELL, DRIP, SCRAPE, DRAG, STEPS, SKITTER, INSECT, KNOCK, DRILL, CLANG, BUMP）、**SPACE**（REVERB）。それぞれにレベル・ON／OFF・パラメーターがあります
- **DREAD**：1 つのつまみ（0〜100）で、和音のうなり、空気の厚み、金属の強さ、砂嵐の不安定さ、イベントの頻度、揺れの深さをまとめて動かします
- 叩く音・こする音は、音程のある発振器ではなく、ノイズや衝撃で共鳴器を鳴らして作っています

### ムードとシード

- **プリセット 24 種**（Ward, Fog Town, Boiler Room, Abyss, Cellar, Rust, Otherworld, Hollow, Dead Channel, Night Radio, Upstairs, Crawlspace, Infestation, Night Shift, Visitor, Presence, Workshop, Pipes, Requiem, Descent, Out of Tune, Threnody, Ritual, Inside）と、SD に保存できるカスタムシーン
- **6 桁のシード**がすべてのランダムな要素を決めます。ムード名とシードを伝えれば、ほかの人の本体で同じ音を鳴らせます（例：`Crawlspace #418207`）
- `R` で、シードからランダムなシーン *Unknown* を作ります

### ひとりでに移り変わる・部屋に反応する

- **Drift**：レイヤーが加わる・抜ける・入れ替わる、数値が動く。どれもゆっくりしたクロスフェードで移ります
- **Arc**：10〜30 分かけて、*Calm* → *Build* → *Peak*（クライマックス）→ *Release* と運びます
- **Listen**：マイクで部屋の音の大きさを測ります（**録音はしません**）。急な物音がすると、音がすっと静まり返ります（Hush）。何も起きない時間が続くと、少しずつ不穏になります
- **Watcher**：Hush が起きると、版画で刷ったようなプロビデンスの目が開き、部屋を見回してから、こちらを見つめます
- **Cursed Mode**：承諾したときだけ、ときどき奇妙なことが起きます（ムードが勝手に変わる、画面がちらつく、入れていない音が忍び込む……）

### 遊ぶ

- **PLANCHETTE**：本体を傾けて、ムードの雰囲気を探ります。気に入ったところで保存・書き出しできます
- **Poltergeist**：本体を隠しておくと、画面を消したまま、ときどきノックや水滴などが鳴ります。人が近くにいると黙り、持ち上げられると気づきます

### 書き出しと録音

- **WAV ループ**：8／16／32／64 秒。*Crossfade*（つなぎ目をなめらかに重ねる）と *Strict*（数学的に周期的なループ）
- **ステム**（ドローンとイベントを別々に）、**バッチ**（連続したシードで 5〜50 個）、**ワンショット**（効果音）
- **ライブ録音**：聴こえている音を、操作ごとそのまま録音します

---

## ハードウェア

| 項目 | 内容 |
|---|---|
| 対応デバイス | **M5Stack Cardputer ADV**（ESP32-S3、PSRAM 不要） |
| 音声 | 内蔵スピーカー + 3.5 mm ジャック、**モノラル** |
| ストレージ | microSD（FAT32。設定・シーン・書き出し・録音） |
| 傾き | 内蔵の IMU（BMI270）。シェイク、PLANCHETTE、Poltergeist、Watcher の視線に使います |
| マイク | 内蔵（ES8311 の入力）。Listen と Poltergeist の Shy で、音量の数値だけを使います |
| LED | 内蔵の RGB LED（WS2812、GPIO21）。Watcher のときに暗い赤で脈打ちます |
| 音を聴くもの | イヤホンがおすすめです（本体のスピーカーでは、HEART や SUB の低い音がほとんど聞こえません） |

> オリジナルの Cardputer（ADV でない方）は動作を確かめていません。IMU がない場合、PLANCHETTE と Poltergeist の When Found は使えません。シェイクの代わりには `Fn`+`S` を使えます。

---

## インストール方法

### 方法 1 — M5Burner でインストール（一番簡単・おすすめ）

ページを開き、ブラウザから書き込みます。

| | M5Burner |
|---|---|
| MIASMORA | https://burner.m5stack.com/share/firmware/3NRPTY |

1. Cardputer ADV を USB-C でパソコンに接続します
2. 上のリンクを、Web Serial に対応したブラウザ（パソコンの Chrome か Edge）で開きます
3. 書き込みのボタンを押し、Cardputer のポートを選びます
4. 書き込みが終わると、MIASMORA が起動します

> MIASMORA は新しい Web 版の M5Burner で公開しています。以前のパソコン用の M5Burner アプリには表示されない場合があります。

### 方法 2 — Launcher FW 経由でのインストール

#### 方法 2a — OTA 機能を使う

1. Cardputer ADV で Launcher FW を起動します
2. OTA 機能から「MIASMORA」を検索します
3. ダウンロード・インストールします

> OTA の一覧に MIASMORA が出てこない場合は、方法 2b をご利用ください。

#### 方法 2b — SD カードに手動でコピーする

1. [Releases](../../releases) ページから、最新の `firmware.bin` をダウンロードします
2. `firmware.bin` を SD カードの**ルート直下**にコピーします（サブフォルダに入れないでください）
3. SD カードを Cardputer ADV に挿し、Launcher FW のファイルブラウザで `firmware.bin` を選んで書き込みます

> Launcher FW から起動した場合、ディープスリープから `G0` で復帰すると、MIASMORA ではなく Launcher に戻ります（復帰は再起動で、起動するのが Launcher のため）。Launcher からもう一度 MIASMORA を起動してください。設定と最後のムードは残っています。方法 1 で書き込んだ場合は、そのまま MIASMORA に戻ります。

### 方法 3 — ソースからビルド（PlatformIO）

1. [VSCode](https://code.visualstudio.com/) に **PlatformIO IDE** 拡張機能を入れます
2. このリポジトリをクローンまたはダウンロードし、フォルダを開きます
3. `platformio.ini` が自動で読み込まれ、初回のビルドで必要なライブラリがダウンロードされます
4. `pio run` でビルドします。環境名は `m5stack-cardputer-adv` です

```bash
pio run -e m5stack-cardputer-adv -t upload
```

ビルドすると、`.pio/build/m5stack-cardputer-adv/` に `firmware.bin`（アプリ本体だけ。Launcher FW 用）と `merge.bin`（ブートローダー + パーティション + アプリをまとめた統合バイナリ。M5Burner など用、`merge_bin.py` が作成）ができます。

> **書き込みモードに入らない場合**：電源オフ → G0 ボタンを押しながら電源オン → G0 ボタンを離す

### 初めて起動したとき（共通）

起動画面のあと、MAIN 画面が出ます（最初は停止した状態です。`Space` で再生）。microSD カードに `/MIASMORA` フォルダが作られ、設定やシーンはそこに保存されます。

SD カードが無くても初期の設定で動きますが、何も保存されず、書き出しと録音は使えません。

---

## 操作方法

画面は MAIN・MOOD・EDIT・EXPORT・SETTINGS の 5 つです。`H` で、ヘルプ（キーの一覧と本体の状態）が出ます。

### どの画面でも使えるキー

| キー | 内容 |
|---|---|
| `Space` | 再生 / 停止（Seed Lock が Off なら、再生を始めるたびに新しいシードになります） |
| `M` / `E` / `X` / `S` | MOOD / EDIT / EXPORT / SETTINGS 画面 |
| `Tab` | 次の画面 |
| `` ` ``（Esc） | 戻る（MAIN へ） |
| `;` / `.` | 項目を選ぶ（上 / 下） |
| `,` / `/` | 値を変える（長押しで速く動く） |
| `R` | ランダムなシーン（*Unknown*） |
| `Shift+R` | ライブ録音の開始 / 停止 |
| `Shift+S` | スリープタイマーの On / Off |
| `Fn+S` | シェイクのアクションを実行 |
| `H` | ヘルプ / About |
| `G0` ボタン | 画面の Off / On（ディープスリープからの復帰も） |

> **Ensō を使っていた方へ**：Ensō の `Shift+S` はシェイクの代わりですが、MIASMORA では **`Shift+S` はスリープタイマー**です。シェイクの代わりは **`Fn+S`** です。

### MAIN 画面

ムード、DREAD（Arc・Listen・Cursed の分を足した値）、ビジュアライザ、シード、再生・録音の状態、スリープタイマー、電池の残りが出ます。Arc が On のときは、ビジュアライザの下に進み具合の細いバーが出ます。

| キー | 内容 |
|---|---|
| `;` / `.` | 音量を上げる / 下げる |
| `,` / `/` | 前 / 次のムード（クロスフェードで切り替わる） |
| `Shift+;` / `Shift+.` | DREAD を上げる / 下げる |
| `0`〜`9` | シードを入力（6 桁目で確定。`Del` で 1 字消す、Esc で取り消し）。*Unknown* と *Drift* は、入力したシードからシーンを作り直す |
| `V` | ビジュアライザを切り替える |
| `Q` | Quick Save：今の音をシーン *Shake_001*、*Shake_002*… として保存（今のシーンはそのまま） |
| `P` | PLANCHETTE |
| `G` | Poltergeist の設定 |

ビジュアライザは本機の出力を映します（マイクの音は映しません）。

| 種類 | 表示 |
|---|---|
| Off | 表示しない |
| Bars | 直近の出力レベルを、上下対称の棒で |
| Scope | 波形。立ち上がりでそろえて止め、大きさは自動で合わせる |
| Monitor | 心電図のように音量の線を描く。イベントや心音の拍はスパイクになる |
| Layers | 鳴っているレイヤーと今の音量を、大きい順に |
| Ink | 中央から赤い染みがにじむ。イベントのたびに滴が飛び、DREAD が高いほど濃く、縁がぎざぎざになる |
| Spectrum | スペクトログラム（約 50 Hz〜6 kHz）。SHEPARD の下がり続ける音や VOX の母音が模様になる |

### MOOD 画面

| キー | 内容 |
|---|---|
| `Enter` | 選んだムードを鳴らす |
| `N` | 今の音を、新しいカスタムシーンとして保存 |
| `D` | カスタムシーンを削除（プリセットと Drift は削除できません） |

プリセットに加えた変更は残りますが（再起動しても）、プリセットそのものは上書きされません。残したいときは `N` で保存してください。カスタムシーンへの変更は、そのファイルに自動で保存されます。

**ムードの共有。** ムード名とシードを伝えてください（例：`Crawlspace #418207`）。プリセットと *Unknown* は、同じムード・同じシードなら同じ音になります。Drift は速さも添えます（`Drift Mid #418207`。同じ道筋になるのは、Drift のほかの設定が初期値で、Arc が Off のとき）。*Unknown* はファームウェアの版も添えてください（`Unknown #418207 v1.0`）。EDIT で作り込んだシーンは、`/MIASMORA/scene/` のファイルを渡してください。

### EDIT 画面

レイヤーの一覧は DRONE / EVENTS / SPACE に分かれています。

| キー | 内容 |
|---|---|
| `;` / `.` | レイヤーを選ぶ |
| `,` / `/` | レベル |
| `O` | On / Off |
| `Enter` | パラメーターの画面を開く（`;` `.` で選び、`,` `/` で変える。Esc で戻る） |

### シェイク

本体を振ると、SETTINGS > Motion > *Shake Action* で選んだアクションが働きます（キーボードでは `Fn+S`）。書き出し中、スリープのフェード中、確認画面を出している間、PLANCHETTE と Poltergeist の間は受け付けません。

| アクション | 内容 |
|---|---|
| Random Scene | `R` と同じ |
| Sleep Timer | スリープタイマーを On。もう一度で Off |
| Display Off | 画面を消す（音は鳴ったまま） |
| New Seed | 新しいシード。音が 1 秒かけて下がり、新しいシードで 1.5 秒かけて戻る |
| Next Mood | 次のムード |
| Hush | Listen の Hush（Listen が Off でも働く） |
| Summon | 鳴っているレイヤーのイベントを 1 つ、今すぐ鳴らす（鳴っている音は切らない） |
| Drift Now | 次の Drift の変化を、今すぐ起こす |
| Quick Save | MAIN の `Q` と同じ |

### スリープタイマー

`Shift+S` で On / Off（電源を入れたときは常に Off）。1〜240 分たつとフェードアウトし（10〜120 秒）、**ディープスリープ**に入るか、再生を止めて画面を消します（SETTINGS > Sleep > After）。ディープスリープからは `G0` で起きます。書き出し中・録音中は、終わるまで待ちます。

---

## サウンドレイヤー

### DRONE（持続音）

| レイヤー | 内容 | パラメーター |
|---|---|---|
| **SUB** | 30〜70 Hz のサイン波を波形整形に通した低音。ピッチが数セントゆっくり漂い、音量が呼吸のように揺れる | Level・Pitch・Drive・Drift |
| **CLUSTER** | 少しずつデチューンした 4 声を、カットオフが動く暗いローパスに通す。うなりが不穏さの核 | Level・Interval（Unison／Minor 2nd／Tritone／Cluster）・Detune・Darkness |
| **AIR** | ピンク／ブラウンノイズを、ゆっくり動くバンドパスに通す。風、ダクト、遠い機械 | Level・Color・Motion・Noise（Pink／Brown） |
| **METAL** | 非整数比の 2 オペレーター FM。錆びた金属、遠い鐘 | Level・Ratio・Index・Motion |
| **MACHINE** | 建物のどこかで動いている機械。*Motor*：回転の唸りと軸受けのゴロゴロ。*Engine*：低く荒いアイドリング、ときどき失火 | Level・Mode（Motor／Engine）・Speed・Load |
| **STATIC** | *TV*：砂嵐と電源ハム。*Radio*：AM ラジオのノイズとパチパチ音。*Tuning*：ヘテロダインの「ピー」と、声のような何かの断片。*Proximity*：「何かが近くにいる」ラジオ | Level・Mode（TV／Radio／Tuning／Proximity）・Tone・Instability |
| **DISCORD** | 4〜6 声の不協和音のパッド。ときどき 1 声だけが、ずり下がってまた戻る | Level・Chord（MinMaj／Dim b9／Poly／Cluster／Tritone）・Pitch・Detune・Darkness・Slide |
| **MICRO** | 12 音の外の音程（四分音、六分音、Bohlen–Pierce など）で組んだパッド。各声が隣の微分音へ滑り、全体がうごめく | Level・Tuning（Quarter／Sixth／BP／Beating／Stretched）・Pitch・Spread・Glide・Wave（Sine／Saw） |
| **VOX** | 言葉のない合唱。母音が言葉にならないほどゆっくり変わる。*Choir*、*Hum*（口を閉じた「ンー」）、*Moan*（うめき声） | Level・Mode（Choir／Hum／Moan）・Pitch・Vowel・Motion・Breath |
| **SHEPARD** | 終わりなく下がり続ける（上がり続ける）音。サイン波 6 本 | Level・Direction（Down／Up）・Speed・Spacing（Octave／Tritone）・Tone |
| **HEART** | 体の内側で聞くような心音「ドッ・クン」と、血流の「サー」。緊張に合わせて速くなり、Hush では 1 拍止まる。Attack で、本体のスピーカーでも拍が聞こえるようにする | Level・BPM・Irregularity・Muffle・Flow・React・Attack |

### EVENTS（不規則な単発音と動き）

どれも **Level** と **Density**（頻度）を持ちます。

| レイヤー | 内容 | そのほかのパラメーター |
|---|---|---|
| **PULSE** | 低い衝撃と短いクリック。ときどき鼓動のような 2 連打 | Weight |
| **CREAK** | 引っかかっては滑るきしみ。床板、古い椅子 | Pitch |
| **SWELL** | 逆再生のように膨らんで、急に切れる | Length |
| **DRIP** | 水滴。ときどき小さな 2 滴目 | Size・Mode（Random／Leak） |
| **SCRAPE** | 爪や金属で表面をなぞる | Material（Wood ↔ Metal）・Pressure |
| **DRAG** | 床の上を何かが引きずられる。*Body*：重いもの。*Pipe*：金属パイプ | Weight・Roughness・Object（Body／Pipe） |
| **STEPS** | 硬い靴底の足音を 4〜12 歩。ときどき途中で止まる…… | Surface（Wood／Concrete／Metal／Gravel）・Direction（Approach／Recede／Pass）・Pace・Muffle |
| **SKITTER** | たくさんの脚が走って、止まって、また走る | Size・Swarm |
| **INSECT** | 不快な虫。Fly、Mosquito、Cicada、Wrong Chirp | Mode・Count |
| **KNOCK** | 木のドアを叩く。*Knock*：何度かノック。*Tap*：数秒おきに軽く | Style（Knock／Tap）・Distance |
| **DRILL** | どこかで電動ドリルやグラインダーを使っている | Pitch・Grind |
| **CLANG** | 叩かれる鉄くず。*Pipe*：パイプ。*Sheet*：ビリビリ鳴る鉄板 | Object（Pipe／Sheet）・Size・Distance |
| **BUMP** | 壁の中や配管で鳴る鈍い「ボッ」 | Pitch・Cluster |

### SPACE

| レイヤー | 内容 | パラメーター |
|---|---|---|
| **REVERB** | Schroeder リバーブ（4 つのコム + 2 つのオールパス）。廃墟や洞窟のような長さまで伸ばせる | Mix・Size・Damping |

---

## ひとりでに移り変わる・部屋に反応する

### Drift

変化し続けるシーンです。待ち時間のあと、レイヤーが加わる・抜ける・入れ替わる、レベルやパラメーターが動く、DREAD が上下する、といった変化が、ゆっくりしたクロスフェードで起きます。変化の流れもシードで決まります。SETTINGS > Drift で、速さ（Slow／Mid／Fast／Custom）、同時に鳴るレイヤーの数、Drift が触ってよいレイヤー（*Drift Pool*）を選べます。

### Arc

10／20／30 分かけて、鳴っているムードを *Calm*（DREAD −20、イベント少なめ）から *Build* を経て *Peak*（DREAD +30、イベント多め）へ運び、*Release* で落ち着かせます。Peak の中で一度だけ**クライマックス**が起きます。シーンのイベントが次々に鳴り、心音が速まり、砂嵐が噴き出して、ブツッと途切れ、数秒の無音のあと Release に入ります。*Once*（1 回で終わる）と *Loop*（繰り返す）を選べ、*Follows Sleep* ではスリープタイマーに長さを合わせます。

### Listen

承諾したときだけ有効になります。マイクで部屋の音の大きさを測ります（**使うのは音量の数値だけで、録音・保存はしません**）。

- 部屋で何も起きない時間が続くほど *Attention*（注意）が高まり、イベントが増え、DREAD が上がります
- 咳・拍手・物音・話し始めの声など、急に音がすると **Hush**：0.3 秒でほぼ無音になり、3〜8 秒そのまま、10〜20 秒かけて戻ります。ときどき、戻る直前にノックや足音が 1 回だけ鳴ります
- 自分の出す音、キーを打つ音、本体を動かしたときの音（IMU で検知）では反応しません
- テレビや会話のように音が続くときは、1 分に 4 回反応したところで 20 秒休みます

### Watcher

Hush が起きると、MAIN のビジュアライザの場所に、版画で刷ったようなプロビデンスの目（骨色のインクと赤い虹彩の片目）が開きます。部屋を見回してから、こちらをじっと見つめ、音が戻るにつれて閉じます。本体を傾けても、こちらを見続けます。そのあいだ LED が暗い赤で脈打ちます。

### Cursed Mode

注意書きに承諾したときだけ有効になります。ときどき（Rare／Normal／Often）奇妙なことが起きます。ムードが勝手に変わる、画面がちらつく・暗くなる、入れていない音が忍び込む、目が一瞬だけ開く、砂嵐のあと「ブチッ」と途切れて静まり返る、心音が止まる……。ファイルが消えるようなことは起きません。書き出し中・録音中、スリープのフェード中は何も起きません。

---

## PLANCHETTE

本体を傾けてムードの雰囲気を探り、気に入ったところで保存するモードです（名前は、ウィジャボードの指示器から）。MAIN の `P` で入ります。入ったときの姿勢が中央です。

| 傾き | 変わるもの |
|---|---|
| 前後 | DREAD +30〜−30 と、イベントの頻度 約 ×2〜×0.5 |
| 左右 | *Shade*：リバーブ。右で深く暗く、左で乾いて明るく |

これが *Axis* 方式です。*Vector* 方式（SETTINGS > Motion > Tilt Mode）では、四隅に置いた 4 つのシーン（初期値は Fog Town、Cellar、Infestation、Otherworld）を、傾きで混ぜます。

| キー | 内容 |
|---|---|
| `Space` | 値を固定（本体を置いてもよい）。もう一度で、今の傾きへゆっくり戻る |
| `C` | 今の姿勢を中央にする |
| `Enter` / `Q` | 今の音をシーン *Tilt_001*、*Tilt_002*… として保存 |
| `Shift+X` | 保存して、聴いていたとおりの音を WAV に書き出す |
| `Esc` | 抜ける（音はそのまま鳴り続ける） |

---

## Poltergeist

いたずら用のモードです。本体をどこかに隠しておくと、ときどきノック、水滴、ひっかく音、足音などが鳴ります。画面も LED も消えているので、どこで鳴っているのか、次にいつ鳴るのかが分かりません。例：KNOCK と DRIP を Random 2〜10 分で、本棚の裏に。

> *あとで一緒に笑える相手に使ってください。Auto Stop は必ず働くので、夜通し鳴り続けることはありません。*

MAIN の `G` で設定のページが開きます。

| 設定 | 内容 |
|---|---|
| Event 1〜3 | 鳴らす音（イベント系のレイヤーか HEART と、その Mode。または None） |
| Timing | *Fixed*（一定の間隔）／*Random*（Min〜Max のあいだ。ときどき数秒おきに 2〜3 回続く） |
| Interval | 10 秒〜30 分（初期値：Random 1〜8 分） |
| Escalate | On：間隔がだんだん短くなり、終わり頃には 40% に |
| Start Delay | 隠しに行くまでの猶予（1〜10 分） |
| Auto Stop | 30 分〜4 時間（Off にはできない） |
| Volume / Room | 音量と、鳴らす音につけるリバーブ |
| Shy | On：人の気配（話し声、足音、咳など）がすると、30 秒〜2 分黙る |
| When Found | Reveal／Silent／???：持ち上げられたときの動き（初期値は Reveal） |
| Test / Start | 1 つ鳴らしてみる / 始める |

始めると、カウントダウンのあいだに隠しに行き（持ち歩いても大丈夫）、本体が静止すると潜伏を始めます。そのあいだはキーを押しても何も起きません。**止めるときは Esc を 2 秒押し続けるか、G0 を押します。** 終わると、潜伏した時間、鳴らした回数、黙った回数、見つかるまでの時間を表示し、`/MIASMORA/poltergeist_log.txt` にも記録します。

イベントとイベントのあいだは省電力になります。約 1 年使った本体では、潜伏中の電池の減りは **Shy が On で 1 時間あたり約 1.5%**、**Off で 1% 未満**でした。

> *Shy は人の気配を聞いています。眠っている人も「人」なので、Shy を On にしてベッドのすぐ横に置くと、寝息や寝返りを聞いて、ほとんど一晩中黙ってしまいます。寝ている部屋で使うときは、Shy を Off にするか、少し離れたところに隠してください。*

---

## 書き出しと録音

### WAV ループの書き出し（EXPORT 画面）

| 設定 | 内容 |
|---|---|
| Loop Mode | *Crossfade*（終わりを始まりに重ね、ループ再生で継ぎ目が消える）／*Strict*（揺れと音程をループ長に合わせた、数学的に周期的なループ） |
| Loop Length | 8／16／32／64 秒 |
| Crossfade | 1／2／4 秒（Crossfade のときだけ） |
| Seed | 今のシード／新しいランダムなシード |
| Drift | Freeze（止めて書き出す）／Record（変化ごと記録） |
| Arc | Ignore（シーンそのものの音）／Apply（書き出しを始めた時点の Arc の値を入れる） |
| Stems | Off／Wet（DRONE と EVENTS を、それぞれのリバーブ込みで）／Dry + Reverb（DRONE・EVENTS・リバーブ成分の 3 つ。足すと元のミックスになる） |
| Batch | Off／5／10／20／50 個。シード、シード + 1…と続けて書き出す。専用のフォルダに入れることも、終わったら眠ることもできる |
| Normalize | Off／On（ピークを −1 dBFS にそろえる） |

`[ Start export ]` で始めます。書き出し中はライブ再生が止まり、Esc で中断できます。EXPORT の Arc が Ignore で、Drift が Freeze なら、同じシード・同じシーンは何度書き出しても同じファイルになります（Arc の Apply は書き出しを始めた時点の Arc の値を、Drift の Record はその時点からの変化を記録するため、書き出すたびに変わります）。

### ワンショット（EXPORT > One-shot）

1 つのレイヤーのイベントを 1 ファイルに 1 つずつ、効果音の素材として `/MIASMORA/oneshot/` に書き出します。Layer、Mode、Unit（1 打だけ／ひとまとまりの動き）、Count（1〜50 個）、Reverb（Dry／Wet）、Normalize を選べます。

### ライブ録音

`Shift+R` で、聴こえている音をそのまま `/MIASMORA/rec/` に録音します。

### ファイル名

```
/MIASMORA/rec/<ムード>_<XF|ST>_<長さ>s_<シード>[_DRx][_ARCxx]_<NNN>[_<ステム>].wav
      例：Cellar_XF_32s_418207_001.wav
/MIASMORA/rec/Batch_<ムード>_<最初のシード>_<NNN>/...   Batch Folder が On のバッチ
/MIASMORA/rec/<ムード>_REC_<NNN>.wav                   ライブ録音
/MIASMORA/oneshot/<レイヤー>[_<Mode>][_G]_<シード>_<NNN>.wav
```

---

## SETTINGS

カテゴリーを `Enter` で開き、`Esc` で戻ります。選んでいる項目の説明が、画面の下に 1 行で出ます。変更はすべて自動で保存されます。

| カテゴリー | 項目 |
|---|---|
| **Sound** | Volume・Seed Lock・Startup（停止で起動／最後のシーンを再生）・Fade In・Startup Sound（起動画面の心音。夜に電源を入れるときは Off に） |
| **Drift** | Speed・（Custom のとき）Wait Min / Max・Transition・Changes Min / Max・Layers Min / Max・Drift Pool・Mode Change |
| **Sleep** | Sleep Timer・Time・Fade・Fade Length・After（Deep Sleep／Display Off） |
| **Display** | Brightness・Dim After・Dim Level・Auto Off・Wake Key・Visualizer・LED・LED Brightness |
| **Motion** | Shake Action・Shake Sens・Tilt Mode・Tilt Range・Tilt Smoothing・Corner A〜D |
| **Listen** | Listen（On にするたびに確認画面）・Sensitivity・Hush Depth・Response・Watcher・Recalibrate |
| **Arc** | Arc・Arc Length・Arc Cycle・Follows Sleep |
| **Cursed** | Cursed Mode（On にするときは確認画面）・Frequency |
| **System** | Debug・Storage・Reset Settings（シーンと WAV は消えない）・About |

**不具合を報告するとき。** System > Debug の *Listen Log* と *Mark Keys* を On にして、不具合を再現し、`Shift+L`（起きるべきでない Hush の直後）か `Shift+K`（反応しなかったとき）で印を付けてください。`/MIASMORA/listen_log.csv` と、Debug の *Performance*・*Memory* の表示を添えて送ってもらえると助かります。

---

## 起動画面

電源を入れたとき（ディープスリープからの復帰も）、暗い赤の霧からロゴが浮かび上がり、心音 4 拍で脈打ち、ゆっくり暗くなって MAIN に移ります（約 7 秒）。心音は HEART のレイヤーと同じ音で、Volume に従います（SETTINGS > Sound > Startup Sound で止められます）。任意のキーか `G0` で飛ばせます（そのキーの操作は行いません）。

---

## 信号経路

```
24 のレイヤー（ノイズ、フィルター、共鳴器、発振器）
    │  ◄── DREAD / Drift / Arc / Listen / Cursed
    ▼
レイヤーのミックス
    │
    ▼
DC カット → リバーブ → マスター音量 → ソフトリミッター
    │
    ▼
TPDF ディザ + 四捨五入 → 16 bit
    │
    ├──► 出力（スピーカー / ヘッドホン）
    └──► ライブ録音（SD）
```

- Level 0 や Off のレイヤーは、処理そのものを省きます
- WAV の書き出しも同じエンジンで合成します（マスター音量は、本体の音量に関係なく固定）

---

## ファイル

```
/MIASMORA/settings.json          本体の設定（自動保存）
/MIASMORA/scene/*.json           カスタムシーン（Shake_NNN、Tilt_NNN も）
/MIASMORA/rec/                   書き出したループ、ステム、バッチ、ライブ録音
/MIASMORA/oneshot/               効果音のワンショット
/MIASMORA/listen_log.csv         Listen の診断ログ（Debug で On のときだけ）
/MIASMORA/poltergeist_log.txt    Poltergeist の結果
```

- シーンのファイルはテキストなので、ほかの人と共有できます

---

## プロジェクト構成

```
MIASMORA/
├── platformio.ini       ビルド設定（環境 m5stack-cardputer-adv）
├── merge_bin.py         ビルド後に merge.bin を作るスクリプト
├── docs/                画像（宣伝用の画像）
├── tools/               ロゴを作るスクリプト（make_logo.py）と、ロゴのフォント（OFL）
└── src/
    ├── main.cpp         setup / loop、起動の順番
    ├── config.h         定数、FW_VERSION
    ├── audio.*          エンジン、ミキサー、マスター処理、音の処理のタスク
    ├── audio_io.*       ES8311 コーデック用の、全二重（出力 + マイク）の I2S ドライバ
    ├── app.*            再生・シード・シーンの管理、スリープタイマー、自動保存
    ├── scene.*          プリセット、ランダムシーン、Drift、DREAD、シーンのファイル
    ├── arc.*            Arc
    ├── listen.*         Listen（Attention、Hush、自分の音の除外）
    ├── cursed.*         Cursed Mode
    ├── planchette.*     PLANCHETTE
    ├── poltergeist.*    Poltergeist（イベント、Shy、省電力、結果）
    ├── export.*         WAV の書き出し（ループ、ステム、バッチ、ワンショット）
    ├── rec.*            ライブ録音
    ├── power.*          ディープスリープ
    ├── led.*            RGB LED
    ├── logo_miasmora.h  起動画面のロゴ（tools/make_logo.py が作成）
    ├── dsp/             サイン表、フィルター、共鳴器、乱数、ディザ、リバーブ
    ├── layers/          25 のレイヤー
    └── ui/              画面、キー、ビジュアライザ、Watcher、起動画面
```

---

## 依存ライブラリ

PlatformIO が自動で管理します（版の指定は `platformio.ini` のとおりです）。

| ライブラリ | 版の指定 |
|---|---|
| `m5stack/M5Cardputer` | 指定なし（最新） |
| `m5stack/M5Unified` | 指定なし（最新） |
| `m5stack/M5GFX` | 指定なし（最新） |
| `bblanchon/ArduinoJson` | ^7.0.0 |
| `adafruit/Adafruit NeoPixel` | ^1.12.0 |

プラットフォームは `espressif32`（Arduino フレームワーク）です。`SD` と `SPI` は Arduino-ESP32 のコアに含まれています。

---

## 既知の制限事項

- 出力はモノラルだけです（Cardputer ADV のジャックは、左右独立の出力に対応していません）
- 書き出し中は、ライブ再生が止まります
- Strict のループでは、鳴り続ける音程がループの長さに合わせて丸められます（8 秒のループなら 1/8 Hz 単位）。ごく小さなデチューンが消えることがあります
- 本体に時計がないので、タイマー類はすべて相対時間です
- レイヤーが増えた版では、同じシードでも *Unknown* と Drift の最初のシーンが変わることがあります（プリセットと保存したシーンは変わりません）
- ディープスリープは完全な電源断ではありません。数日かけて少しずつ電池が減ります。止めたいときは本体の電源スイッチを切ってください
- Listen は急な音にだけ反応します。ゆっくり大きくなる音や、続いている話し声の途中には反応しません。本機の音とぴったり重なった音、雑音が主役のムード（Dead Channel など）、最大音量のときは、反応しにくくなります
- HEART と SUB の一番低い音は、本体のスピーカーではほとんど聞こえません（イヤホンや書き出したファイルでは鳴っています）
- Launcher FW から起動した場合、ディープスリープから復帰すると Launcher に戻ります（方法 2b の注を参照）

---

## ライセンス

**CC BY-NC-SA 4.0**（表示 - 非営利 - 継承）。全文は [LICENSE](LICENSE) を参照

- **表示（BY）**：利用の際はクレジット表記をお願いします
- **非営利（NC）**：商用利用はできません
- **継承（SA）**：改変・二次利用したものを公開する場合、同じ CC BY-NC-SA 4.0 で公開してください

詳細は [https://creativecommons.org/licenses/by-nc-sa/4.0/deed.ja](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.ja) をご覧ください。

起動画面のロゴの書体は **UnifrakturMaguntia**（j. 'mach' wust 作。Peter Wiegel のフォントをもとにしたもので、原型は Carl Albert Fahrenwaldt の 1901 年の書体）で、**SIL Open Font License 1.1** で公開されています。ファームウェアにはロゴの画素のデータだけが入っています。フォントとライセンス文（`OFL.txt`）は `tools/fonts/` にあります。

作: [@Tokagetchi](https://x.com/Tokagetchi)、コード: Claude（Anthropic）。[M5Unified / M5GFX / M5Cardputer](https://github.com/m5stack)、[ArduinoJson](https://github.com/bblanchon/ArduinoJson)、[Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) と PlatformIO の上に作っています。

自由に使用・改変・共有してください（商用利用を除く）。不気味な音ができたら、ぜひコミュニティにシェアしてください！
