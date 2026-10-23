#include "layer.h"

Layer* createSub();
Layer* createCluster();
Layer* createAir();
Layer* createMetal();
Layer* createStatic();
Layer* createPulse();
Layer* createCreak();
Layer* createSwell();
Layer* createDrip();
Layer* createScrape();
Layer* createDrag();
Layer* createSteps();
Layer* createSkitter();
Layer* createInsect();
Layer* createKnock();
Layer* createMachine();
Layer* createDrill();
Layer* createClang();
Layer* createBump();
Layer* createDiscord();
Layer* createMicro();
Layer* createVox();
Layer* createShepard();
Layer* createHeart();

Clock gClk;

// REVERB is not a Layer object: it is the engine's master-stage effect.
Layer* createLayer(int id) {
  switch (id) {
    case L_SUB:     return createSub();
    case L_CLUSTER: return createCluster();
    case L_AIR:     return createAir();
    case L_METAL:   return createMetal();
    case L_STATIC:  return createStatic();
    case L_PULSE:   return createPulse();
    case L_CREAK:   return createCreak();
    case L_SWELL:   return createSwell();
    case L_DRIP:    return createDrip();
    case L_SCRAPE:  return createScrape();
    case L_DRAG:    return createDrag();
    case L_STEPS:   return createSteps();
    case L_SKITTER: return createSkitter();
    case L_INSECT:  return createInsect();
    case L_KNOCK:   return createKnock();
    case L_MACHINE: return createMachine();
    case L_DRILL:   return createDrill();
    case L_CLANG:   return createClang();
    case L_BUMP:    return createBump();
    case L_DISCORD: return createDiscord();
    case L_MICRO:   return createMicro();
    case L_VOX:     return createVox();
    case L_SHEPARD: return createShepard();
    case L_HEART:   return createHeart();
    default:        return nullptr;
  }
}
