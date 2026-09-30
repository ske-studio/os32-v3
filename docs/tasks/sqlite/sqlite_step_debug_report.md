# SQLite #UD クラッシュ — 根本原因分析レポート

## 1. 現象

カーネル起動時の SQLite テスト（`CREATE TABLE`）で **#UD (Invalid Opcode, Vector 6)** 例外が発生し、システムが停止する。

### 例外ハンドラ出力（スクリーンショットより）

| 項目 | 値 | 備考 |
|------|-----|------|
| Exception | #UD Invalid Opcode | Vector 0x06 |
| Error Code | 0x00000000 | ダミー（#UDにはerr codeなし） |
| Fault EIP | **0x0021FD84** | **[OUT OF CODE!]** — コード領域外 |
| EAX | 0x001F6CC5 | `.sqlite_bss` 内 (sqlite_mem_pool + 0x11BE5) |
| EBX | 0x001F70B0 | `.sqlite_bss` 内 |
| ECX | 0x00000000 | |
| EDX | 0x001F6CC5 | EAX と同値 |
| ESI | 0x021F6C60 | 不正値（16MB超、ページング範囲外） |
| EDI | 0x00000001 | |
| EBP | **0x0000006C** | フレームポインタ完全破壊 |
| ESP | **0x0021FD84** | 代替スタック内（TOP-620B） |

### 重要な観察

- **EIP = ESP = 0x0021FD84**: EIPとESPが同じ値。スタック上のデータをリターンアドレスとして読み取り、スタック領域にジャンプしている
- **0x0021FD84** は代替スタック `MEM_SQLITE_STACK` (0x200000〜0x21FFF0) 内
- **Stack Trace が空**: EBP = 0x6C < 0x9000 のためフレームチェーンを追跡できない
- スタックダンプに **0x001A4295** が存在 → **`zeroPage`** 関数内 (SQLite B-tree ページ初期化)

---

## 2. 根本原因: `kmemset` のスタック不整合バグ

### バグの場所

`lib/kstring_asm.asm#L102-L110` の `kmemset` 関数、`.aligned` パス:

```diff
 .aligned:
     push ecx          ;; ECX をスタックに保存
     shr ecx, 2        ;; DWORD数に変換
-    jz .do_bytes_recover  ;; 0なら飛ぶ → ★ pop ecx をスキップ!
+    jz .do_bytes_recover  ;; 0なら残りバイト処理へ
     rep stosd         ;; DWORD単位書き込み
     pop ecx           ;; ← ★ ECX>>2 == 0 の場合ここを通過しない!
 
 .do_bytes_recover:
     and ecx, 3
     jz .done
```

### 発生条件

**`shr ecx, 2` の結果が 0 になるケース**:

1. アライン処理後に残りバイト数が **1〜3** の場合
2. 例: `kmemset(aligned_ptr, 0, 5)` でアドレスが3バイトずれていると、アライン処理で3バイト消費 → 残り2 → `shr 2, 2` = 0

具体的な呼び出し元は `zeroPage()` (SQLite B-tree):

```asm
;; zeroPage+0x60: kmemset の2回目の呼び出し
1a4283:  push   %eax           ;; size (数バイト程度)
1a4284:  push   $0x4           ;; ← 固定値 4
1a4286:  push   $0x0           ;; val = 0
1a4288:  lea    0x1(%esi,%edx,1),%eax  ;; dst
1a428f:  ...
1a4290:  call   917e <kmemset>
1a4295:  mov    -0x20(%ebp),%edx  ;; ← スタックダンプにこのアドレスが存在!
```

### クラッシュまでの連鎖

```
zeroPage() → kmemset(ptr, 0, size) を呼ぶ
  ↓
kmemset: アライン処理後の残りが 1〜3 バイト
  push ecx (=残りバイト数)
  shr ecx, 2 → ECX = 0
  jz .do_bytes_recover → pop ecx をスキップ!
  ↓
.done:
  pop edi   ← 実際には push された ECX を pop (EDI 破壊)
  pop ebp   ← 旧 EDI を pop (EBP = 不正値)
  ret       ← 旧 EBP を pop (リターンアドレスではない!)
  ↓
不正アドレス (代替スタック内のデータ) にジャンプ → #UD
```

### なぜ kmemcpy では同じバグがないか

`kmemcpy` の `.aligned` パスでは **`push` せずに `edx` レジスタに保存**しています：

```asm
;; kmemcpy (正しい実装)
.aligned:
    mov edx, ecx      ;; ← レジスタに退避（push しない）
    shr ecx, 2
    jz .do_bytes_recover
    rep movsd

.do_bytes_recover:
    mov ecx, edx      ;; ← レジスタから復元
    and ecx, 3
```

`kmemset` では `edx` が戻り値保存に使われているため `push ecx` を使っていますが、`pop` のパスが不完全でした。

---

## 3. 修正方針

### 方法A: `jz` の飛び先を `pop ecx` の直後に変更

```asm
.aligned:
    push ecx
    shr ecx, 2
    jz .ms_pop_recover    ;; ← 変更
    rep stosd
.ms_pop_recover:
    pop ecx               ;; ← 常に pop される
    and ecx, 3
    jz .done
```

### 方法B: push/pop をやめてスタックフレーム上に退避

```asm
.aligned:
    mov [ebp-8], ecx      ;; ローカル変数に保存
    shr ecx, 2
    jz .do_bytes_recover
    rep stosd

.do_bytes_recover:
    mov ecx, [ebp-8]
    and ecx, 3
    jz .done
```

> [!IMPORTANT]
> **方法A を推奨**。最小の変更で修正でき、パフォーマンスへの影響もありません。

---

## 4. 修正によって解決される問題

| 問題 | 状態 |
|------|------|
| #UD Invalid Opcode (CREATE TABLE時) | ✅ 解決見込み |
| EBP/ESP 破壊による Stack Trace 空白 | ✅ 解決見込み |
| 以前の #PF Page Fault (FPU init前) | △ FPU初期化追加で #PF → #UD に変化。根本はkmemset |

> [!WARNING]
> **`kmemset` はカーネル全体で使用されている汎用関数**です。このバグは SQLite 以外の箇所でもサイズ条件が合えば発生し得ます。これまで顕在化しなかったのは、大半の呼び出しで4バイト以上のアライン済みサイズが渡されていたためと推測されます。

---

## 5. ISR スタブ修正（今回実施済み）

`kernel/isr_stub.asm` のスタックオフセット計算バグも同時に修正済み:

- `error_code`, `vector`, `fault_eip` が正しく `exception_handler` に渡されるようになった
- `regs` ポインタ（PUSHAD配列）も引数として渡すように拡張
- レジスタダンプ・EIP領域判定・EBPチェーンスタックトレース・ESPスタックダンプを追加

---

## 6. 次のアクション

1. **`kmemset` のバグを修正** （方法Aで `pop ecx` を常に通過させる）
2. **`make clean && make all`** で全体リビルド
3. **デプロイ → NP21/W 再起動 → SQLite テスト再実行**
4. スタックトレースが正常に出力され、CREATE TABLE が成功することを確認
