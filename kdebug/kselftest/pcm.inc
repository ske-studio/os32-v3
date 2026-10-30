/* ------------------------------------------------------------------------ */
/*  PCM (票 TASK_PCM_CS4231): 起動時の検出が走った後、driver が CLOSED で    */
/*  待っていること。装置の有無は機種で変わるので**状態だけ**を見る           */
/*  (NP21/W の既定構成には CS4231 が無い — 無くても壊れないのが要件)。       */
/*  ついでに、知らないレートは**装置に 1 バイトも書かずに**断ること。        */
/* ------------------------------------------------------------------------ */
static void test_pcm(void)
{
    check(pcm_state() == PCM_ST_CLOSED, "pcm:closed");
    check(pcm_open(PCM_RATE_44100 + 1) == OS32_ERR_INVAL, "pcm:rate refused");
    check(pcm_state() == PCM_ST_CLOSED, "pcm:still closed");
}
