// spawn_star_exit.c.inc

void bhv_ccm_touched_star_spawn_loop(void) {
    if (gCCMEnteredSlide & 1) {
        if ((gMarioStates[0].action & ACT_GROUP_MASK) == ACT_GROUP_CUTSCENE) { return; }
        o->oPosY += 100.0f;
        o->oPosX = 2780.0f;
        o->oPosZ = 4666.0f;
        f32* starPos = gLevelValues.starPositions.CcmSlideStarPos;
        spawn_default_star(starPos[0], starPos[1], starPos[2]);
        gCCMEnteredSlide &= ~1;
        obj_mark_for_deletion(o);
    }
}
