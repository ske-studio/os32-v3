# コメント処理テスト
echo test1
// スラッシュコメント
echo test2
# ハッシュコメント
echo test3

# if テスト
if hello == hello echo PASS-if-eq
if hello != world echo PASS-if-neq
if hello == world echo FAIL-should-not-print

# goto テスト
goto SKIP
echo FAIL-should-skip
:SKIP
echo PASS-goto

# return テスト
echo DONE
return
echo FAIL-after-return
