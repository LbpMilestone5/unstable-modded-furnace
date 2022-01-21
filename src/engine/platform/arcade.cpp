#include "arcade.h"
#include "../engine.h"
#include <string.h>
#include <math.h>

static unsigned short chanOffs[8]={
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};
static unsigned short opOffs[4]={
  0x00, 0x08, 0x10, 0x18
};
static bool isOutput[8][4]={
  // 1     3     2    4
  {false,false,false,true},
  {false,false,false,true},
  {false,false,false,true},
  {false,false,false,true},
  {false,false,true ,true},
  {false,true ,true ,true},
  {false,true ,true ,true},
  {true ,true ,true ,true},
};
static unsigned char dtTable[8]={
  7,6,5,0,1,2,3,0
};

static int orderedOps[4]={
  0,2,1,3
};

#define rWrite(a,v) if (!skipRegisterWrites) {pendingWrites[a]=v;}
#define immWrite(a,v) if (!skipRegisterWrites) {writes.emplace(a,v); if (dumpWrites) {addWrite(a,v);} }

void DivPlatformArcade::acquire_nuked(short* bufL, short* bufR, size_t start, size_t len) {
  static int o[2];

  for (size_t h=start; h<start+len; h++) {
    if (!writes.empty() && !fm.write_busy) {
      QueuedWrite& w=writes.front();
      if (w.addrOrVal) {
        OPM_Write(&fm,1,w.val);
        //printf("write: %x = %.2x\n",w.addr,w.val);
        writes.pop();
      } else {
        OPM_Write(&fm,0,w.addr);
        w.addrOrVal=true;
      }
    }
    
    OPM_Clock(&fm,NULL,NULL,NULL,NULL);
    OPM_Clock(&fm,NULL,NULL,NULL,NULL);
    OPM_Clock(&fm,NULL,NULL,NULL,NULL);
    OPM_Clock(&fm,o,NULL,NULL,NULL);

    pcmCycles+=31250;
    if (pcmCycles>=rate) {
      pcmCycles-=rate;

      // do a PCM cycle
      pcmL=0; pcmR=0;
      for (int i=8; i<13; i++) {
        if (chan[i].pcm.sample>=0) {
          DivSample* s=parent->song.sample[chan[i].pcm.sample];
          if (!isMuted[i]) {
            if (s->depth==8) {
              pcmL+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolL);
              pcmR+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolR);
            } else {
              pcmL+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolL)>>8;
              pcmR+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolR)>>8;
            }
          }
          chan[i].pcm.pos+=chan[i].pcm.freq;
          if (chan[i].pcm.pos>=(s->rendLength<<8)) {
            if (s->loopStart>=0 && s->loopStart<=(int)s->rendLength) {
              // Sega PCM limitation
              chan[i].pcm.pos=(s->loopStart&(~0xff))<<8;
            } else {
              chan[i].pcm.sample=-1;
            }
          }
        }
      }
    }

    o[0]+=pcmL;
    o[1]+=pcmR;
    
    if (o[0]<-32768) o[0]=-32768;
    if (o[0]>32767) o[0]=32767;

    if (o[1]<-32768) o[1]=-32768;
    if (o[1]>32767) o[1]=32767;
  
    bufL[h]=o[0];
    bufR[h]=o[1];
  }
}

void DivPlatformArcade::acquire_ymfm(short* bufL, short* bufR, size_t start, size_t len) {
  static int os[2];

  for (size_t h=start; h<start+len; h++) {
    os[0]=0; os[1]=0;
    if (!writes.empty()) {
      if (--delay<1) {
        QueuedWrite& w=writes.front();
        fm_ymfm->write(0x0+((w.addr>>8)<<1),w.addr);
        fm_ymfm->write(0x1+((w.addr>>8)<<1),w.val);
        writes.pop();
        delay=1;
      }
    }
    
    fm_ymfm->generate(&out_ymfm);

    pcmCycles+=31250;
    if (pcmCycles>=rate) {
      pcmCycles-=rate;

      // do a PCM cycle
      pcmL=0; pcmR=0;
      for (int i=8; i<13; i++) {
        if (chan[i].pcm.sample>=0 && chan[i].pcm.sample<parent->song.sampleLen) {
          DivSample* s=parent->song.sample[chan[i].pcm.sample];
          if (!isMuted[i]) {
            if (s->depth==8) {
              pcmL+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolL);
              pcmR+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolR);
            } else {
              pcmL+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolL)>>8;
              pcmR+=(s->rendData[chan[i].pcm.pos>>8]*chan[i].chVolR)>>8;
            }
          }
          chan[i].pcm.pos+=chan[i].pcm.freq;
          if (chan[i].pcm.pos>=(s->rendLength<<8)) {
            if (s->loopStart>=0 && s->loopStart<=(int)s->rendLength) {
              // Sega PCM limitation
              chan[i].pcm.pos=(s->loopStart&(~0xff))<<8;
            } else {
              chan[i].pcm.sample=-1;
            }
          }
        }
      }
    }

    os[0]=out_ymfm.data[0]+pcmL;
    if (os[0]<-32768) os[0]=-32768;
    if (os[0]>32767) os[0]=32767;

    os[1]=out_ymfm.data[1]+pcmR;
    if (os[1]<-32768) os[1]=-32768;
    if (os[1]>32767) os[1]=32767;
  
    bufL[h]=os[0];
    bufR[h]=os[1];
  }
}

void DivPlatformArcade::acquire(short* bufL, short* bufR, size_t start, size_t len) {
  if (useYMFM) {
    acquire_ymfm(bufL,bufR,start,len);
  } else {
    acquire_nuked(bufL,bufR,start,len);
  }
}

unsigned char noteMap[12]={
  0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14
};

int hScale(int note) {
  return ((note/12)<<4)+(noteMap[note%12]);
}

void DivPlatformArcade::tick() {
  for (int i=0; i<8; i++) {
    if (chan[i].keyOn || chan[i].keyOff) {
      immWrite(0x08,i);
      chan[i].keyOff=false;
    }
  }

  for (int i=0; i<256; i++) {
    if (pendingWrites[i]!=oldWrites[i]) {
      immWrite(i,pendingWrites[i]&0xff);
      oldWrites[i]=pendingWrites[i];
    }
  }

  for (int i=0; i<8; i++) {
    if (chan[i].freqChanged) {
      chan[i].freq=chan[i].baseFreq+(chan[i].pitch>>1)-64;
      immWrite(i+0x28,hScale(chan[i].freq>>6));
      immWrite(i+0x30,chan[i].freq<<2);
      chan[i].freqChanged=false;
    }
    if (chan[i].keyOn) {
      immWrite(0x08,0x78|i);
      chan[i].keyOn=false;
    }
  }

  for (int i=8; i<13; i++) {
    if (chan[i].freqChanged) {
      chan[i].freq=chan[i].baseFreq+(chan[i].pitch>>1)-64;
      if (chan[i].furnacePCM) {
        chan[i].pcm.freq=MIN(255,((440.0*pow(2.0,double(chan[i].freq+256)/(64.0*12.0)))*255)/31250);
      }
      chan[i].freqChanged=false;
    }
  }
}

void DivPlatformArcade::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  if (ch<8) {
    DivInstrument* ins=parent->getIns(chan[ch].ins);
    if (isMuted[ch]) {
      rWrite(chanOffs[ch]+0x20,(ins->fm.alg&7)|(ins->fm.fb<<3));
    } else {
      rWrite(chanOffs[ch]+0x20,(ins->fm.alg&7)|(ins->fm.fb<<3)|((chan[ch].chVolL&1)<<6)|((chan[ch].chVolR&1)<<7));
    }
  }
}

int DivPlatformArcade::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      if (c.chan>7) {
        if (skipRegisterWrites) break;
        if (ins->type==DIV_INS_AMIGA) {
          chan[c.chan].pcm.sample=ins->amiga.initSample;
          if (chan[c.chan].pcm.sample<0 || chan[c.chan].pcm.sample>=parent->song.sampleLen) {
            chan[c.chan].pcm.sample=-1;
            break;
          }
          chan[c.chan].pcm.pos=0;
          chan[c.chan].baseFreq=c.value<<6;
          chan[c.chan].freqChanged=true;
          chan[c.chan].furnacePCM=true;
        } else {
          chan[c.chan].pcm.sample=12*sampleBank+c.value%12;
          if (chan[c.chan].pcm.sample>=parent->song.sampleLen) {
            chan[c.chan].pcm.sample=-1;
            break;
          }
          chan[c.chan].pcm.pos=0;
          chan[c.chan].pcm.freq=MIN(255,(parent->song.sample[chan[c.chan].pcm.sample]->rate*255)/31250);
          chan[c.chan].furnacePCM=false;
        }
        break;
      }

      for (int i=0; i<4; i++) {
        unsigned short baseAddr=chanOffs[c.chan]|opOffs[i];
        DivInstrumentFM::Operator op=ins->fm.op[i];
        if (isOutput[ins->fm.alg][i]) {
          if (!chan[c.chan].active || chan[c.chan].insChanged) {
            rWrite(baseAddr+0x60,127-(((127-op.tl)*(chan[c.chan].vol&0x7f))/127));
          }
        } else {
          if (chan[c.chan].insChanged) {
            rWrite(baseAddr+0x60,op.tl);
          }
        }
        if (chan[c.chan].insChanged) {
          rWrite(baseAddr+0x40,(op.mult&15)|(dtTable[op.dt&7]<<4));
          rWrite(baseAddr+0x80,(op.ar&31)|(op.rs<<6));
          rWrite(baseAddr+0xa0,(op.dr&31)|(op.am<<7));
          rWrite(baseAddr+0xc0,(op.d2r&31)|(op.dt2<<6));
          rWrite(baseAddr+0xe0,(op.rr&15)|(op.sl<<4));
        }
      }
      if (chan[c.chan].insChanged) {
        if (isMuted[c.chan]) {
          rWrite(chanOffs[c.chan]+0x20,(ins->fm.alg&7)|(ins->fm.fb<<3));
        } else {
          rWrite(chanOffs[c.chan]+0x20,(ins->fm.alg&7)|(ins->fm.fb<<3)|((chan[c.chan].chVolL&1)<<6)|((chan[c.chan].chVolR&1)<<7));
        }
        rWrite(chanOffs[c.chan]+0x38,((ins->fm.fms&7)<<4)|(ins->fm.ams&3));
      }
      chan[c.chan].insChanged=false;

      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=c.value<<6;
        chan[c.chan].freqChanged=true;
      }
      chan[c.chan].keyOn=true;
      chan[c.chan].active=true;
      break;
    }
    case DIV_CMD_NOTE_OFF:
      if (c.chan>7) {
        chan[c.chan].pcm.sample=-1;
      }
      chan[c.chan].keyOff=true;
      chan[c.chan].active=false;
      break;
    case DIV_CMD_VOLUME: {
      chan[c.chan].vol=c.value;
      if (c.chan>7) {
        chan[c.chan].chVolL=c.value;
        chan[c.chan].chVolR=c.value;
        break;
      }
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      for (int i=0; i<4; i++) {
        unsigned short baseAddr=chanOffs[c.chan]|opOffs[i];
        DivInstrumentFM::Operator op=ins->fm.op[i];
        if (isOutput[ins->fm.alg][i]) {
          rWrite(baseAddr+0x60,127-(((127-op.tl)*(chan[c.chan].vol&0x7f))/127));
        } else {
          rWrite(baseAddr+0x60,op.tl);
        }
      }
      break;
    }
    case DIV_CMD_GET_VOLUME: {
      return chan[c.chan].vol;
      break;
    }
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].insChanged=true;
      }
      chan[c.chan].ins=c.value;
      break;
    case DIV_CMD_PANNING: {
      // TODO
      if (c.chan>7) {
        chan[c.chan].chVolL=(c.value>>4)|(((c.value>>4)>>1)<<4);
        chan[c.chan].chVolR=(c.value&15)|(((c.value&15)>>1)<<4);
      } else {
        DivInstrument* ins=parent->getIns(chan[c.chan].ins);
        chan[c.chan].chVolL=((c.value>>4)==1);
        chan[c.chan].chVolR=((c.value&15)==1);
        if (isMuted[c.chan]) {
          rWrite(chanOffs[c.chan]+0x20,(ins->fm.alg&7)|(ins->fm.fb<<3));
        } else {
          rWrite(chanOffs[c.chan]+0x20,(ins->fm.alg&7)|(ins->fm.fb<<3)|((chan[c.chan].chVolL&1)<<6)|((chan[c.chan].chVolR&1)<<7));
        }
      }
      break;
    }
    case DIV_CMD_PITCH: {
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    }
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=c.value2<<6;
      int newFreq;
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        newFreq=chan[c.chan].baseFreq+c.value;
        if (newFreq>=destFreq) {
          newFreq=destFreq;
          return2=true;
        }
      } else {
        newFreq=chan[c.chan].baseFreq-c.value;
        if (newFreq<=destFreq) {
          newFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].baseFreq=newFreq;
      chan[c.chan].freqChanged=true;
      if (return2) return 2;
      break;
    }
    case DIV_CMD_LEGATO: {
      chan[c.chan].baseFreq=c.value<<6;
      chan[c.chan].freqChanged=true;
      break;
    }
    case DIV_CMD_FM_LFO: {
      if (c.chan>7) break;
      rWrite(0x18,c.value);
      break;
    }
    case DIV_CMD_FM_LFO_WAVE: {
      if (c.chan>7) break;
      rWrite(0x1b,c.value&3);
      break;
    }
    case DIV_CMD_FM_MULT: {
      if (c.chan>7) break;
      unsigned short baseAddr=chanOffs[c.chan]|opOffs[orderedOps[c.value]];
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      DivInstrumentFM::Operator op=ins->fm.op[orderedOps[c.value]];
      rWrite(baseAddr+0x40,(c.value2&15)|(dtTable[op.dt&7]<<4));
      break;
    }
    case DIV_CMD_FM_TL: {
      if (c.chan>7) break;
      unsigned short baseAddr=chanOffs[c.chan]|opOffs[orderedOps[c.value]];
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      if (isOutput[ins->fm.alg][c.value]) {
        rWrite(baseAddr+0x60,127-(((127-c.value2)*(chan[c.chan].vol&0x7f))/127));
      } else {
        rWrite(baseAddr+0x60,c.value2);
      }
      break;
    }
    case DIV_CMD_FM_AR: {
      if (c.chan>7) break;
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      if (c.value<0)  {
        for (int i=0; i<4; i++) {
          DivInstrumentFM::Operator op=ins->fm.op[i];
          unsigned short baseAddr=chanOffs[c.chan]|opOffs[i];
          rWrite(baseAddr+0x80,(c.value2&31)|(op.rs<<6));
        }
      } else {
        DivInstrumentFM::Operator op=ins->fm.op[orderedOps[c.value]];
        unsigned short baseAddr=chanOffs[c.chan]|opOffs[orderedOps[c.value]];
        rWrite(baseAddr+0x80,(c.value2&31)|(op.rs<<6));
      }
      break;
    }
    case DIV_CMD_FM_AM_DEPTH: {
      amDepth=c.value;
      immWrite(0x19,amDepth);
      break;
    }
    case DIV_CMD_FM_PM_DEPTH: {
      pmDepth=c.value;
      immWrite(0x19,0x80|pmDepth);
      break;
    }
    case DIV_CMD_STD_NOISE_FREQ: {
      if (c.chan!=7) break;
      if (c.value) {
        if (c.value>0x1f) {
          rWrite(0x0f,0x80);
        } else {
          rWrite(0x0f,0x80|(0x1f-c.value));
        }
      } else {
        rWrite(0x0f,0);
      }
      break;
    }
    case DIV_CMD_SAMPLE_BANK:
      sampleBank=c.value;
      if (sampleBank>(parent->song.sample.size()/12)) {
        sampleBank=parent->song.sample.size()/12;
      }
      break;
    case DIV_ALWAYS_SET_VOLUME:
      return 0;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 127;
      break;
    case DIV_CMD_PRE_PORTA:
      break;
    case DIV_CMD_PRE_NOTE:
      break;
    case DIV_CMD_SAMPLE_FREQ:
      chan[c.chan].pcm.freq=c.value;
      break;
    default:
      //printf("WARNING: unimplemented command %d\n",c.cmd);
      break;
  }
  return 1;
}

void DivPlatformArcade::forceIns() {
  for (int i=0; i<13; i++) {
    chan[i].insChanged=true;
  }
  immWrite(0x19,amDepth);
  immWrite(0x19,0x80|pmDepth);
}

void DivPlatformArcade::notifyInsChange(int ins) {
  for (int i=0; i<8; i++) {
    if (chan[i].ins==ins) {
      chan[i].insChanged=true;
    }
  }
}

void DivPlatformArcade::reset() {
  while (!writes.empty()) writes.pop();
  if (useYMFM) {
    fm_ymfm->reset();
  } else {
    memset(&fm,0,sizeof(opm_t));
    OPM_Reset(&fm);
  }
  for (int i=0; i<13; i++) {
    chan[i]=DivPlatformArcade::Channel();
    chan[i].vol=0x7f;
  }

  for (int i=0; i<256; i++) {
    oldWrites[i]=-1;
    pendingWrites[i]=-1;
  }

  lastBusy=60;
  pcmCycles=0;
  pcmL=0;
  pcmR=0;
  sampleBank=0;
  delay=0;
  amDepth=0x7f;
  pmDepth=0x7f;

  //rWrite(0x18,0x10);
  immWrite(0x19,amDepth);
  immWrite(0x19,0x80|pmDepth);
  //rWrite(0x1b,0x00);

  extMode=false;
}

bool DivPlatformArcade::isStereo() {
  return true;
}

void DivPlatformArcade::setYMFM(bool use) {
  useYMFM=use;
}

int DivPlatformArcade::init(DivEngine* p, int channels, int sugRate, bool pal) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  for (int i=0; i<13; i++) {
    isMuted[i]=false;
  }
  if (useYMFM) {
    rate=447443/8;
    fm_ymfm=new ymfm::ym2151(iface);
  } else {
    rate=447443;
  }
  reset();

  return 13;
}

void DivPlatformArcade::quit() {
  if (useYMFM) {
    delete fm_ymfm;
  }
}

DivPlatformArcade::~DivPlatformArcade() {
}
